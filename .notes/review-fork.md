# Review: the fork's diff against upstream (97722d48f9..HEAD)

Scope: everything the fork adds or changes, from the crash catcher, the core, the Vulkan backend, the
shaders, the denoiser and upscalers, and the game side of the seam to upstream files, the harness and
the tests. Evidence beside the review: register statistics from a release bench (RTX 4090 Laptop,
`seyda-neen-ship` + `balmora-mages-guild`, headless), and one `./omw test` run for timings.

Dropped after checking:
- the texture level bias's missing `− 1.0` (DLSS guide §3.5): measured and refused, `reconstruction.hpp:202-214`;
- SDL3 pausing a covered window on X11: SDL3's X11 backend raises `OCCLUDED` only with minimize or
  `_NET_WM_STATE_HIDDEN` (`SDL_x11events.c:660,2168`, `SDL_x11window.c:313`), never on
  `VisibilityFullyObscured`;
- the presenter tests failing in this run: the run used SDL's offscreen driver, which has no surface to
  present to (the same family as the open issue in `ISSUES.md`).

## 1. Proposal

### Root causes

1. **The crash catcher's protocol is timed, not acknowledged.** The game and the monitor share bare
   counters on a page (`mHangAsked`, `mHangReports`, `mNotesRead`, `mMonitorAnswer`) and wait on each
   other with a dozen independent sleep-polls (1 ms × 2000, 10 s, 5 s, 60 s…). No wait carries a
   request id, and none checks whether the other side is alive. Of the ~107 fixes to the catcher,
   about half move one of those limits or add a state for a race that the next slower machine (ASan,
   CI, cold disk) reopens: 4fa040f851, 8d5d8b663e, ffcf309fa3, 10d8792c60, d04761ae68, a051b99100,
   e934d86f3e, 96477fb01d.
2. **Hang and report dumps are taken in-process through Crashpad's crash path.**
   `CRASHPAD_SIMULATE_CRASH` (`crashpadclient.cpp:90`) runs the crash handler on a live game. On Linux
   it shares the client's one `exception_information_`; on Windows `DumpWithoutCrash` waits with no
   limit. The gate states, the saved kind and reason, both first-chance `onFault` waits, the reporter
   thread and its `atexit` stop exist only to make that safe (0d9859cc1b, 60456d45b8, 69ca79e6ab,
   4061fde7e9).
3. **Diagnostics stop where the fork's attention stopped.**
   - Crash notes cover the renderer's frame but not:
     - the frame wait where a lost device is found;
     - target or swapchain rebuilds;
     - NGX calls;
     - pictures;
     - the game's loading work.
   - An error names the Vulkan call, not its subject: an out-of-memory error says nothing of what was
     allocated or how full the heap was.
   - The cheap breadcrumbs are off in shipped builds.
   - A refusal names a NIF node ("Tri Shape 0"), never the file.
   - A slow session logs no frame-time summary.
   - The startup log lacks most of what decides the picture.
4. **Load work has no bound.** A world is one batch, staged whole and flushed as one submit
   (`vulkanrenderer.cpp:476-497`, `commands.cpp:343-367`):
   - host staging peaks at the whole world;
   - the GPU work behind one wait is unbounded against a fixed 10 s patience (`result.hpp:46`).

   The cell ring adopts a whole cell a frame, so the largest cell sets the worst frame.
5. **A check is applied on one path and skipped on its siblings.**
   - The content store verifies an entry in `read` but trusts a `stat` in `holds`.
   - The frame walk refuses a non-finite place, but the cell ring's template walk does not.
   - `checkVk` has a device overload and a deviceless one, and the deviceless one drops the lost-device
     path.
   - `getCapabilities` takes the surface-lost path, but the formats and support queries throw.
   - The cache clear in `awaitShaders` runs only when preparation outlives the kernels.
   - Settings are deferred to the frame for `upscale` but applied at once, under `noexcept`, for bounce
     rays and vsync.
6. **Vendor contracts are spelled outside their owner.**
   - DLSS-RR traces at FSR's ratios, though NGX's optimal settings are required and `optimalSettings` is
     never called.
   - Vendor branches sit on a public `DriverProfile::mVendor`.
   - The DLSS evaluation leaves an image in a layout its hand-off does not report.
7. **Occupancy is judged by registers alone.**
   - Trace workgroups are one 32-lane warp, so the per-SM block cap (16 on Turing and Ampere, 24 on
     Ada) binds before registers do.
   - The denoiser's heaviest passes are bound by shared tiles held expanded out of their compact
     encodings.
   - NVIDIA's spill figure (`Local Memory Size` = 2^36 + bytes) is discarded, so the spill gate is blind
     there.
8. **Choices written per field instead of chosen once.**
   - The harness re-lists `RtxSettingValues` three times by hand, string-keyed for the shipped
     defaults, and forgot `mUpscaler`, so `view` never runs the player's DLSS.
   - The GPU test fixture restores a hand-kept list of renderer state that misses the reserved upscales
     and the fog field.
   - "Did the extent move" is answered in five places.
9. **Identity strings and duplicated knowledge.**
   - A chain's identity is composed as strings in three places.
   - The game's model rules are copied by hand into `contentmodels.cpp`.
   - Two class caches answer one question.
   - Jumps are matched by node address across frames.
10. **Tests lean on time and unbounded loops.**
    - Wall-clock asserts and sleeps prove negatives.
    - Several waits have no bound, so a regression hangs to CTest's timeout.
    - The gate allows 2 s a test with no suite bound. Two tests take 29.7 s and 27.6 s; the suite takes
      86 s.

### Target design

**Crash catcher.**
- **An acknowledged protocol** (removes root cause 1).
  - Every request on the page carries a sequence number that the responder echoes.
  - Every wait is "until echoed, or the other side is dead". On Linux that is a pidfd of the
    handler's pid; on Windows a `SYNCHRONIZE` handle on the pid the monitor writes with its answer. A
    fixed cap stays only as a last resort, from one constant.
  - This is Crashpad's own shape: `ExceptionHandlerClient::SignalCrashDump` waits only while the
    handler's socket is alive.
- **Hang dumps taken by the monitor from outside the game** (removes root cause 2).
  - The tools: `ProcessSnapshotLinux` over a `DirectPtraceConnection`, or `ProcessSnapshotWin` under
    `ScopedProcessSuspend`, then `MinidumpFileWriter` into the database.
  - This is vendored Crashpad's `tools/generate_dump.cc`, which the fork already links, and the shape
    of Firefox's `HangMonitorParent::TakeBrowserMinidump` and Breakpad's out-of-process writer.
  - What goes: the reporter thread, `takeHangReports`, `stopReporter`, the gate's Hang and Report
    states, `sSavedKind`/`sSavedReason`, both first-chance `onFault` waits, `Crash::report` and the
    matrix's nondeterministic `fault-under-report` mode.
  - On Linux the monitor runs `ExceptionHandlerServer` with a delegate that takes one mutex around the
    stock `CrashReportExceptionHandler`, so a ptrace attach for a hang never overlaps a crash dump.
    That is about 100 lines of what `handler_main.cc` does today. Windows needs no lock, because
    suspend counts nest.
- **The note table travels as one Crashpad annotation.**
  `crashpad::Annotation(UserDefinedType(1), "openmw-notes", &table)`, about 9 KB, under the 20 KiB
  value limit. The per-slot seqlock stays.
  - What goes: `--openmw-notes`, `MonitorArguments::mNotes` and the raw `ProcessMemory::Read`.
  - What it adds: every `.dmp` carries the notes, so `omw crash` prints them when the player sent only
    the dump.
- **A clean-exit word on the page.** The game writes its exit code at the end of `wrapApplication`, so
  the monitor can say "ended without reaching its end and left no report". That makes an OOM kill or an
  outside `SIGKILL` visible on POSIX, where `exitCode()` is `nullopt` today. This is Sentry's "abnormal
  session".
- **Optional, later: the monitor as the game's parent**, merged with the AppImage keeper.
  - What goes: the double fork, the subreaper, the `/proc` scan, the CLOEXEC dance, `awaitMonitor` and
    pid reuse.
  - What it adds: `waitpid` gives the exit status.
  - Costs:
    - a debugger started on the executable attaches to the monitor;
    - the launcher waits until the crash box closes;
    - a Windows `CreateProcess` path.
  - Proposed for the user's decision, not planned.
- The remaining mutable statics go on one never-destroyed `Catcher` owner. Only the note table and its
  gate stay global, because a signal handler reads them.

**Diagnostics for a player's report** (removes root cause 3).
- **Errors carry their subject and the thread's note.** `DeviceError`, `InputError` and
  `UpscalerRefused` capture `Crash::ownNote()` at construction. The note has unwound by the time the
  fatal box shows, and a thrown error leaves no dump.
- **Allocation errors.** The allocator's `take` throws with the resource name, bytes, `MemoryUse` and
  `describeMemory(report())`.
- **One `checkVk(device, …)` wherever a device exists.** A loss always appends the fault, the markers,
  the memory report and the submits in flight.
- **Always-on diagnostics are the cheap ones:**
  - AMD buffer markers, in device-uncached memory where `VK_AMD_device_coherent_memory` is offered;
  - `VK_EXT_device_fault`;
  - crash notes on every cold path;
  - one log line per swapchain or target rebuild with its reason.

  NV checkpoints follow after an A/B. Submit watching and counters stay opt-in.
- **Refusals name the model file.** A refusal carries the model path, in its text and its dedupe key.
  The path comes from the model root, through the `StableIdentity` stamp the game already sets, or
  through the loader (an upstream change).
- **The played session logs a frame-time summary.** Median, p99, worst and hitch count from a
  fixed-size histogram on `RtxRenderer`, using `rank`/`summarise` moved from
  `apps/rtxtool/instruments/frametimes.cpp` into the core. One line at session end and one at each
  cell change.
- **A tested `describe()` of the derived run setup**, logged once where the renderer is made.
- **One log tag for the renderer and one `mebibytes()`.**

**Bounded load work** (removes root cause 4).
- A staging ring of fixed size, 64–256 MiB. When it fills, the batch submits, and the next record
  waits on the oldest chunk.
  - What it bounds: host memory, the GPU time behind each wait, and the window a fault is reported in.
  - Who does it this way: UE's upload ring, Granite's transfer queue, DXVK's staging allocator.
- The cell ring adopts by a budget of parts or vertices that resumes inside a cell.

**One check per rule** (removes root cause 5).
- **The content store.**
  - Its format goes into its folder (`rtx-content/v<format>/`), so two builds never delete each
    other's entries.
  - One trailer-check primitive serves both `read` and `holds`.
  - Payloads are built outside the lock and moved in.
- **One stand check, "may this part stand"** (a finite place, `checkFits`), called by the frame walk
  and the template walk.
- **Settings reach the renderer at the next `renderFrame`**, as `upscale` does, so every allocation
  failure takes the one path the player is told about in words.
- **One phase rule for every world event.**

**Upscaler entity owns its contract** (removes root cause 6).
- Each backend answers its own extents for a mode: FSR's ratios, or NGX's optimal settings.
- Each backend returns the state it left every image it was handed (`HandedImage&` in and out).
- `Moves` from `FrameTracer::movesTo` is the one answer to "what moved", handed to each resize.
- `DriverProfile` states capabilities ("runs NGX"), not a vendor. DLSS's instance and device extension
  branches move into the NVIDIA upscaler entity.

**Occupancy as the hardware limits it** (removes root cause 7).
- Trace stages run 64-lane workgroups, two tiles each, with
  `tile = gl_WorkGroupID.x * 2 + gl_SubgroupID`. Ballots and appends are already per subgroup, and no
  trace stage has a barrier.
- Shared tiles hold texels in their source encoding: a normal code, a distance, an RGB9E5 word,
  `packHalf2x16`. The result is bit-exact.
- The spill gate reads NVIDIA's local-memory bytes.
- Register levers come after, confirmed with Nsight's per-instruction register view:
  - regenerate a ray's request per turn instead of carrying it;
  - store results before fog and sprites;
  - keep stage records packed.

**One typed source per choice** (removes root causes 8 and 9).
- `RtxSettingValues::fromShipped` sits beside `fromRegistry`, the source is chosen once, and the line
  overrides arrive as `std::optional`s merged in one place.
- The test fixture's reset covers every setter by construction, with RAII guards.
- A `ChainIdentity` struct is made by the pass, and the claim set and `TextureTable` key on it.
- The model list is derived from the `MWClass` answers.
- One `std::type_index`-keyed class cache.
- Jumps are keyed by `StableIdentity`.

**Tests on events, not time** (removes root cause 10).
- Injected clocks and wake hooks.
- Every test loop bounded, failing with what was pending.
- One RAII per-test folder.
- The gate holds 1 s a test and 30 s a suite, and fails a suite that wrote no report.

### Shared ownership

| case | verdict |
|---|---|
| `std::shared_ptr<OffscreenView>` (`LocalMap::mView`, `PendingPaints::Pending::mTile`, `MapOverlay::paintTile`) | **Kept.** `LocalMap` may drop or replace a tile while a paint still waits for its copy, so the two lifetimes overlap dynamically. |
| `std::make_shared<std::vector<ref_ptr<…>>>` into `SceneUtil::UnrefQueue` (`worldmirror.cpp:385`) | **Kept.** It is upstream's queue API (`shared_ptr<void>`), and the worker drops the batch. |
| Content store resident entries (proposed `std::shared_ptr<const Misc::ByteBuffer>`) | **New, justified.** The bytes are immutable, a reader decodes them outside the lock, and `discard` or a replacement erases the entry concurrently. |
| `Surface` held by `VulkanRenderer` (`unique_ptr`) and mutated through `Presenter`'s `Surface&` (`presenter.cpp:188`) | **Replace:** made first for device selection, then moved into `Presenter`, its one owner. `Surface` becomes a `Handled<…>`. |
| `Device`'s `const` getters returning mutable `Memory`/`Pool`/`Graveyard`, and `Timeline::waitFor const` over `mutable mFinished` | **Replace:** holders that allocate, bury or submit take `Device&`, and the getters are non-const. |
| `RtxRenderer::mGui` (raw, dangling from `~WindowManager` to `~RtxRenderer`) | **Replace:** a detach call nulls it, and `freezeFrame` asserts it as the others do. |
| `FrameDescriber::mJumped` (raw `const osg::Node*` across a frame) | **Replace:** keyed by the `StableIdentity` stamp, sorted and binary-searched. |
| `Renderer`'s screenshot writer in a `ref_ptr`, documented as borrowed (`renderer.hpp:556`) | **Replace:** a reference. |
| The crash catcher's ~14 mutable statics (`crashpadclient.cpp:43-116`) | **Replace:** one never-destroyed `Catcher`. Only the note table and its gate stay global, for the signal handler. Most go with the out-of-process hang dumps. |

## 2. Plan

Batches are ordered by impact, each after any batch it needs. Items marked **(upstream)** change
upstream code and wait for the user's yes, then a line in `.notes/UPSTREAM.md`.

### Batch 1 — crash catcher: an acknowledged protocol

`components/crashcatcher/{crashpadclient,crashpadclientposix,crashpadclientwin32,crashpadmonitor,crashpadmonitorposix,crashpadmonitorwin32,crashpage,crashnote,crashmonitorlog}.*`, `apps/components_tests/crashcatcher/*`

- [ ] **Sequence numbers and liveness.**
  - Where: `crashpadclient.cpp:88-103`, `:148-157`, `:223`; `crashpage.hpp`.
  - Defect:
    - bare counters with no request id: a report that times out at 10 s and reads late puts back the
      next report's note table early, the 4fa040f851 class reopened;
    - a Windows monitor that died leaves `DumpWithoutCrash` waiting forever;
    - a slow start leaves Crashpad installed and the fork's layer off, while the log says the monitor
      did not start.
  - Target: each request carries a sequence number the monitor echoes. Every wait runs until echoed or
    the other side is dead: a pidfd of the handler on Linux, an `OpenProcess(SYNCHRONIZE)` handle on
    Windows. One last-resort cap constant.
- [ ] **The fault wait is a deadline.**
  - Where: `crashpadclientposix.cpp:28`, `crashpadclientwin32.cpp:53`.
  - Defect: 2000 × `nanosleep(1ms)` / `Sleep(1)`. Windows rounds `Sleep(1)` to 15.6 ms, so up to ~31 s
    with a frozen game.
  - Target: a deadline over `clock_gettime` / `QueryPerformanceCounter`, from the one cap constant.
- [ ] **The player hears of a crash whose dump failed.**
  - Where: `crashpadmonitor.cpp:275-276`, `:497-499`.
  - Defect: the box and the package are gated on `!dumps.empty()`. A crash whose dump write failed
    (disk full, ptrace denied) shows nothing, and the game just vanishes.
  - Target: tell the player and package the log whenever `mCrashed || mEnded`, naming the missing dump.
- [ ] **No lost monitor lines.**
  - Where: `crashmonitorlog.cpp:104`.
  - Defect: blocks queued before `setLogFile` are discarded: a crash summary, "Crash package: …",
    "The game ended with …".
  - Target: the monitor keeps its own log in the reports folder, and appends to the game's log once it
    is known.
- [ ] **Crashpad's own errors reach the log.**
  - Where: `crashpadmonitor.cpp:453-462`.
  - Defect: Crashpad's `LOG(ERROR)` (ptrace failures, database errors) goes to an inherited stderr.
  - Target: `logging::SetLogMessageHandler` into `MonitorLog`.
- [ ] **A clean-exit word on the page.**
  - Where: `crashpadmonitorposix.cpp:58`.
  - Defect: `exitCode` is always `nullopt`, so an OOM or outside `SIGKILL` leaves no line.
  - Target: the game writes its exit code at the end of `wrapApplication`. Its absence logs "ended
    without reaching its end and left no report".
- [ ] **Refuse a monitor that cannot read the game.**
  - Where: `crashpadmonitor.cpp:432` (`probeDatabase`).
  - Defect: the probe never checks Yama. Under `ptrace_scope` ≥ 2 every dump fails at crash time.
  - Target: read `/proc/sys/kernel/yama/ptrace_scope` in `install` and refuse with that reason.
- [ ] **Note table sized for the pools.**
  - Where: `crashnote.hpp:18`.
  - Defect: `sNoteThreads = 32` against `usableCpuCount()-1` prepare hands plus main, ring, chain and
    compile threads. In the bulk decode the threads past 32 note nothing, silently.
  - Target: size the table for the pools, or give pools stable slots, and count refused claims so the
    summary says "N threads found the table full".
- [ ] **A scope ends on its own thread's slot.**
  - Where: `crashnote.cpp:178-186`.
  - Defect: `~NoteScope` dereferences `ownSlot()`. A scope held as a member (`PipelineCreation::mNoted`)
    and destroyed on another thread writes the wrong slot, or dereferences null.
  - Target: the scope keeps its `Slot*`, and a debug assert checks the owning thread.
- [ ] **No box queued behind a crash.**
  - Where: `crashpadmonitor.cpp:465-476`.
  - Defect: a crash queued behind an open hang box waits for the player to answer a box about a game
    already gone.
  - Target: drop a pending `AskToEnd` once `HandlerReturned` is queued.
- [ ] **The monitor's life after the game has a bound.**
  - Where: `crashpadmonitor.cpp:465`.
  - Defect: it is unbounded. A handler stuck attaching keeps the monitor, the keeper and the AppImage
    mount alive.
  - Target: once the game is gone, give `HandlerMain` a bounded time, log, and exit.
- [ ] **Install and end are logged.**
  - Where: `crashpadclient.cpp:249`, `debugging.cpp:513-520`.
  - Target:
    - at install: the monitor's pid, the Yama scope, the AppImage keeper;
    - at the monitor's end: always one line (reports N, exit code or signal or unknown).
  - After the renderer starts, check that `SIGSEGV`/`SIGABRT` are still Crashpad's, and log otherwise.
    Sentry Native reinstalls for this case.
- [ ] **`annotate` names its thread.**
  - Where: `crash.hpp`.
  - Defect: `annotate` is not thread-safe (`SimpleStringDictionary`).
  - Target: its doc says main thread only, and a debug assert holds it.
- [ ] **Matrix tests on events.**
  - Where: `apps/components_tests/crashcatcher/crashtests.cpp:138-181,291,316`,
    `crashtestsposix.cpp:220`.
  - Defect: the waits are wall-clock: a 2 s limit, 30 s and 5 s waits, `sleep 4; kill -CONT`.
  - Target: wait on the echoed ids.
  - New cases:
    - the box and package when the dump is missing;
    - the monitor killed mid-session, then `fatal`: a prompt abort plus a log line;
    - an outside `SIGKILL`: the exit-word line;
    - the note table full;
    - a cross-thread `NoteScope` dies on its assert.

### Batch 2 — crash catcher: hang dumps from outside, notes as an annotation (after 1)

`components/crashcatcher/*`, `tools/omw/crash.py`, the matrix

- [ ] **Hang dumps from outside the game.**
  - Where: `crashpadclient.cpp:90` (`CRASHPAD_SIMULATE_CRASH`) and the reporter thread.
  - Defect: hang and report dumps go through the crash path in-process.
  - Target: the monitor takes them itself, as `generate_dump.cc` does. On Linux a monitor-side
    `ExceptionHandlerServer` delegate holds one mutex around the stock handler.
  - Delete: the reporter thread, `takeHangReports`, `stopReporter` and its `atexit`, `mHangAsked`,
    `mHangReports`, `mNotesRead`, the gate's Hang and Report states, `sSavedKind`/`sSavedReason`,
    `beginReport`/`endReport`, `takeForFault`, both first-chance `onFault` waits, and `Crash::report`.
    `crash.hpp:61` has no caller.
  - Delete in the matrix: `fault-under-report` and `sInconclusive`.
- [ ] **The note table as an annotation.**
  - Where: `crashmonitorarguments.*`, `crashpadsummarysource.cpp`.
  - Defect: the raw table address is passed on the command line and read with `ProcessMemory::Read`.
  - Target: one `crashpad::Annotation` of the table.
- [ ] **One owner for the client's state.**
  - Where: `crashpadclient.cpp:43-116`.
  - Defect: the mutable statics that remain.
  - Target: one never-destroyed `Catcher` owner.
- [ ] **`omw crash` prints what the log said.**
  - Where: `tools/omw/crash.py`.
  - Defect: it prints only the stack walk.
  - Target: also print the OMW summary stream (`0x4F4D5701`), the simple annotations and the
    notes annotation.
- [ ] **The summary lines up with the log.**
  - Where: `crashpadsummarysource.cpp:331`.
  - Defect: the summary has no uptime or frame count.
  - Target: add both (`mFrames` is already read).

### Batch 3 — diagnostics in the renderer (fork code)

`components/rtx/common/error.hpp`, `components/rtxvulkan/{device,present,upscale}/*`, `framering.cpp`, `picturetracer.cpp`, `vulkanrenderer.cpp`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp`, `components/myguirtx/slottexture.cpp`

- [ ] **A thrown error carries the thread's note.**
  - Where: `rtx/common/error.hpp:45`.
  - Defect: `DeviceError`, `InputError` and `UpscalerRefused` unwind every note before the fatal box,
    and no dump exists.
  - Target: capture `Crash::ownNote()` into the message at construction.
- [ ] **Out-of-memory names its subject.**
  - Where: `device/memory/memory.cpp:231-270`, `memory/buffer.cpp:171`, `gui/guitextures.cpp:49`.
  - Defect: the error names only the VMA call.
  - Target: `take` throws with the name, bytes, `MemoryUse` and `describeMemory(report())`.
- [ ] **A lost device anywhere takes the fault path.**
  - Where: `present/swapchain.cpp:185`.
  - Defect: the deviceless `checkVk` loses `deviceFailed`/`describeFault` on `VK_ERROR_DEVICE_LOST`.
  - Target: `checkVk(mDevice, …)` wherever a device exists, and the deviceless overload only for
    instance and physical-device calls. A source-tree test holds it.
- [ ] **A lost device names what was in flight.**
  - Where: `device.cpp` `deviceFailed`.
  - Defect: the line carries nothing beyond the fault.
  - Target: append the memory report and the submits in flight (`getNext()-1` vs
    `getKnownFinished()`).
- [ ] **Breadcrumbs on by default where they are cheap.**
  - Where: `rtx/renderer/renderer.hpp:120`, `device.cpp:168-169`.
  - Defect: breadcrumbs are off in shipped builds.
  - Target: AMD markers on by default. NV checkpoints on after a bench A/B.
- [ ] **Markers that survive a hang.**
  - Where: `device/buffermarkers.cpp:14`, `memoryTypesFor`.
  - Defect: markers live in cached memory and may be stale after a hang.
  - Target: where `VK_AMD_device_coherent_memory` is offered, enable it and allocate the markers
    `DEVICE_COHERENT|DEVICE_UNCACHED`.
- [ ] **Validation failures report their message.**
  - Where: `device/validation.cpp:47`.
  - Defect: `std::abort()`.
  - Target: `Crash::fatal(text)`.
- [ ] **Pictures get zones.**
  - Where: `picturetracer.cpp:122`.
  - Defect: pictures record with no timer, so no checkpoint or marker, and a fault is blamed on the
    last world zone.
  - Target: checkpoint-only zones for pictures, with a frame number that marks them as pictures.
- [ ] **A checkpoint per pass, behind a switch.**
  - Where: `pipeline/dispatch.hpp:233`, `device/gputimer.cpp:95`.
  - Defect: one checkpoint a zone: "Upscale" is all of FSR or NGX.
  - Target: `dispatch()` sets a checkpoint holding the pipeline's name, behind a runtime switch.
- [ ] **Crash notes on the cold paths.**
  - At the frame wait, `framering.cpp:163-171`: "waiting for frame N: mode, traced WxH to WxH, filter,
    upscaler".
  - `VulkanRenderer::createTargets` (`:275`).
  - `Presenter::rebuild`/`remake`/`remakeSurface` (`presenter.cpp:174-200`).
  - The `PipelineCache` constructor, naming the file a player can delete.
  - `GuiTextures::add`.
  - The NGX init, create-feature and evaluate calls (`dlssupscaler.cpp:204,265,605`, `ngx.cpp:249`).
  - The upscaler constructors (`dlssupscaler.cpp:398`, `fsrupscaler.cpp:476`).
  - `RtxRenderer::applyChangedSettings` (`rtxrenderer.cpp:794`).
- [ ] **Annotations that say where the player was.**
  - Where: `rtxrenderer.cpp` and `vulkanrenderer.cpp`.
  - Defect: only the version, renderer, device, driver and layers are annotated.
  - Target: add "upscaler" and "extents" where targets are made; "worldspace" on
    `notifyWorldspaceChanged` and on a cut; "content" (file count, PBR companion maps); "video driver";
    "appimage".
- [ ] **Swapchain rebuilds leave a trail.**
  - Where: `swapchain.cpp:118`, `presenter.cpp:174-200`.
  - Defect: only the first swapchain is logged, without its format, colour space or extent.
  - Target: one line per create and recreate: reason, extent, image count, format, colour space, mode,
    present fences and present wait.
- [ ] **The log says what was taken.**
  - Where: `physicaldevice.cpp:550-558`, `device.cpp:196-204`.
  - Defect: `describe()` lists the options offered, not the ones taken.
  - Target: `Device` logs the taken `DeviceOption` set once.
- [ ] **Mode changes name everything that decides the picture.**
  - `vulkanrenderer.cpp:296`: the target-change line adds the upscaler backend, the light filter, and
    whether the chain was rebuilt or kept.
  - `dlssupscaler.cpp:477`: feature creation at Info, with extents, quality, bytes and the library file.
  - `dlssupscaler.cpp:608`: the failure adds extents, the reset flag and the frame.
- [ ] **History resets keep their cause.**
  - Where: `rtx/frame/framepast.hpp`, `vulkanrenderer.cpp:882`.
  - Defect: a history reset leaves no trace.
  - Target: `FramePast` keeps its `HistoryLoss`, and one Verbose line names the cause and what was
    reset.
- [ ] **Unique pipeline names.**
  - Where: `denoise/shadowpass.cpp:33`.
  - Defect: 9 pipelines are named "shadow-filter".
  - Target: a name per level and field set.
- [ ] **A played session logs its frame times.**
  - Where: `rtxrenderer.cpp:277`.
  - Defect: no frame-time summary, and the comment cites a "periodic line" that does not exist.
  - Target: an allocation-free histogram, one line at session end and at each cell change, with
    `rank`/`summarise` moved from `apps/rtxtool/instruments/frametimes.cpp:88,121` into the core.
- [ ] **The startup log describes the run.**
  - Where: `rtxrenderer.cpp:111-186,280`.
  - Defect: the derived profile, validation level, watch-submits, breadcrumbs, backend override, build
    type, shader digest and companion-map count are not logged.
  - Target: one tested `describe()`, logged once.
- [ ] **One tag, one unit.**
  - Where: the log tags across `components/rtx*`; the MiB conversions in `preparedimages.cpp:170-171`,
    `pipelinecache.cpp:341`, `physicaldevice.cpp:87,535`, `benchrecord.cpp:335`, `worldmirror.cpp:331`.
  - Defect: tags vary ("Rtx: ", "Ray tracing …", "Vulkan:", none). Integer division prints "0 MiB",
    and `Rtx::megabytes` returns MiB.
  - Target: one tag; one `mebibytes()` returning a float.
- [ ] **Compressed interface textures decode or warn.**
  - Where: `myguirtx/slottexture.cpp:119-157`.
  - Defect: a BC-compressed interface DDS goes through `getColor` per texel. BC4–7 come out as
    garbage, with no warning.
  - Target: decode with the core's decoder, or upload the BC data natively. At least warn.
- [ ] **A thread past the note table is not silent.**
  - Where: `crashnote.cpp`.
  - Target: the full-table count from batch 1 shows in the summary. This covers the 31 prepare hands.

### Batch 4 — content robustness and the content store

`components/rtx/prepare/*`, `components/contentstore/store.cpp`, `components/rtx/scene/refusals.cpp`, `components/rtx/mirror/*`

- [ ] **A corrupt manifest is refused, not thrown on.**
  - Where: `prepare/preparationmanifest.cpp:120-125`, `:85`.
  - Defect: names are not checked. A non-normalized name throws from `NormalizedView` on the prepare
    worker, terminating the game at every start until the manifest is deleted. Confirmed.
  - Target: refuse the manifest when `!VFS::Path::isNormalized(name)`, and hold the names as
    `VFS::Path::Normalized` past the parse.
- [ ] **The prepare job's note opens first.**
  - Where: `contentpreparation.cpp` `walkJob`.
  - Defect: `standingDigest` runs before the "preparing the model" note.
  - Target: open the note at the top of `walkJob`.
- [ ] **The store's format is its folder.**
  - Where: `store.cpp:247-251` (`holds`), `:478`, `:506-511`, `:519`.
  - Defect:
    - `holds` trusts a `stat`, so after a format bump every chain "stands" and is recomputed in play;
    - two builds of different `sFormat` delete each other's entries.
  - Target: `rtx-content/v<format>/`, and one trailer-check primitive for `read` and `holds`.
  - Also: separate the `holds` counters from `read`'s `mFound`/`mMissed`.
- [ ] **No copies under the store's lock.**
  - Where: `store.cpp:197-205`, `:238`, `:357-374`, `:292`.
  - Defect: MB copies run under `mMonitor`, and the frame thread's `Reach::Memory` asks wait behind
    them. The comment at `:282` ("copied nowhere") is false.
  - Target: build outside the lock and move in; resident entries as `shared_ptr<const ByteBuffer>`.
- [ ] **Store failures are logged.**
  - Where: `store.cpp:488-495`, `:519-524`.
  - Defect: a read error other than `TooLarge` is a silent miss, and a corrupt file is deleted with no
    log.
  - Target: log once a session the first refusal and the first read error, with the path and the check
    that failed.
- [ ] **Refusals name the model file.**
  - Where: `scene/refusals.cpp:46-57` and its callers (`meshresolver.cpp:241,429`,
    `emitterresolver.cpp:93,130,337`, `sceneextractor.cpp:393,579,837,901`); `materialresolver.cpp:405-406`.
  - Defect: dedupe is on the node name, so a second mod's broken "Tri Shape 0" is never logged. All
    nameless images share one line.
  - Target: a model field on `Refusal`, in the key and the text. The path comes from the model root's
    stamp, or **(upstream)** from the VFS path `SceneManager` sets beside `sFileHash`.
- [ ] **The frame walk's mesh read is noted.**
  - Where: `meshresolver.cpp:134,248`, `sceneextractor.cpp:333-357`.
  - Defect: the mesh read (fold, crease split) runs with no note, and there is no note per reference
    root.
  - Target: notes naming the drawable and its model, and the reference's stamp.
- [ ] **Parts in distant cells pass the same check.**
  - Where: `mirror/cells/templatewalk.cpp:179`, `cellplacer.cpp:407`.
  - Defect: template parts stand at non-finite places, which the frame walk refuses
    (`sceneextractor.cpp:899`).
  - Target: one stand check that both walks call.
- [ ] **One walk per model for neighbouring shapes.**
  - Where: `mirror/meshreader.cpp:446-495`.
  - Defect: `gatherBeside` walks the whole model per drawable, O(S²), and a store hit pays it too.
  - Target: walk once per model, cached beside `mFacesOf`.
- [ ] **The ring adopts by a budget.**
  - Where: `mirror/cells/cellring.cpp:366-371`.
  - Defect: a whole cell a frame.
  - Target: a parts or vertex budget that resumes inside a cell.
- [ ] **One identity for a chain.**
  - Where: `contentpreparation.cpp:470-499`, `materialresolver.cpp:442-448`.
  - Defect: the identity is composed as strings in three places.
  - Target: one `ChainIdentity` used by the claim set and `TextureTable::mPathIndex`.
- [ ] **One class cache.**
  - Where: `nodekind.cpp`, `materialresolver.cpp:100-118`.
  - Defect: two caches, one keyed by literal address and one by `&typeid`, and a false comment in
    `nodekind.hpp`.
  - Target: one cache on `std::type_index`.
- [ ] **A doc with the wrong width.**
  - Where: `texturetable.hpp:255`.
  - Defect: says "sixteen takes"; `RefusedTakes` is 24 bits.
  - Target: the doc matches the 24 bits.
- [ ] **Tests for this batch:**
  - a manifest with a non-normalized name;
  - two models with one drawable name, both reported;
  - `holds` on a stale or corrupt file;
  - a write failure logged once;
  - the `sResidentBytesMost` bound;
  - template parts at non-finite places;
  - `describeImage`'s refusals tested beside `image/`.

### Batch 5 — bounded load and presentation robustness

`components/rtxvulkan/{vulkanrenderer.cpp,device/commands.cpp,device/timeline.cpp,device/result.hpp,present/*,device/requirements.cpp,device/physicaldevice.cpp,texture/texture.cpp}`

- [ ] **A staging ring.**
  - Where: `vulkanrenderer.cpp:476-497`, `commands.cpp:343-367`.
  - Defect: a world build stages the whole world, as `Essential` memory, then one submit.
  - Target: a fixed ring that submits as it fills, with `trimStaging` gone from the world path.
  - Test: staging bytes during a world build stay within the ring.
- [ ] **The patience covers bounded work.**
  - Where: `result.hpp:46`, `timeline.cpp:54`.
  - Defect: 10 s against an unbounded submit, so a slow card's Tamriel Rebuilt build reads as a lost
    device.
  - Target: the chunks above, or re-arm the wait while the timeline advances.
- [ ] **A lost surface takes the lost-surface path.**
  - Where: `surface.cpp:74,90`, `swapchain.cpp:223`.
  - Defect: `VK_ERROR_SURFACE_LOST_KHR` from the formats or support query throws, so a hot-plug ends
    in the fatal box.
  - Target: these queries answer a lost case, as `getCapabilities` does.
  - Test: surface lost from each query.
- [ ] **Present fences under either spelling.**
  - Where: `requirements.cpp` `sPresentFences`, `instance.cpp:86-91`.
  - Defect: only the KHR extension names, so present fences are silently off on drivers from before
    the promotion.
  - Target: take the `EXT` spellings too, as DXVK does, and log which was taken.
- [ ] **Descriptor limits are checked.**
  - Where: `physicaldevice.cpp:256-379` (`profileOf`), `texture.cpp:93-101`.
  - Defect: the 2×8192 update-after-bind images and samplers are never held to the device's limits.
  - Target: an obstacle, as `maxPushConstantsSize` has.
- [ ] **The format matches with its colour space.**
  - Where: `swapchain.cpp:47`.
  - Defect: the format is matched without its colour space.
  - Target: match the (format, `SRGB_NONLINEAR`) pair, as vk-bootstrap does.
- [ ] **The surface has one owner.**
  - Where: `vulkanrenderer.hpp:176`, `presenter.cpp:188`.
  - Defect: two holders.
  - Target: moved into `Presenter`; `Surface` as a `Handled<…>`.
- [ ] **`const` means unchanged.**
  - Where: `device.hpp` getters, `Timeline::waitFor`.
  - Defect: `const` getters return mutable subsystems.
  - Target: `Device&` for holders that mutate.
- [ ] **The shader folder is read once.**
  - Where: `pipelinecache.cpp:131`, `rtx/renderer/shaderdirectory.cpp:25`, `shadercode.cpp:205`, the
    census.
  - Defect: the folder is read three times.
  - Target: one `ShaderSet` loaded at device creation lends spans and the digest.
- [ ] **Device-level dispatch.**
  - Where: `instance.cpp:258`.
  - Defect: only `volkLoadInstance`, so every command goes through the loader.
  - Target: `volkLoadDevice` (or a `VolkDeviceTable`) for the one device.
- [ ] **The graveyard's contract is asserted.**
  - Where: `retiring.hpp:16`.
  - Defect: nothing asserts that stamps do not fall, and the deque is hand-rolled.
  - Target: a debug assert, and a ring.
- [ ] **The diagnostic watch has a bound.**
  - Where: `commands.cpp:37-62` (`watchToItsEnd`).
  - Defect: an unbounded loop.
  - Target: a bound.
- [ ] **Tests for this batch:**
  - `chooseFormat` and `presentModeFor` moved out of the anonymous namespace and tested in a table;
  - a pure formatter split out of `describeFault`/`describeCheckpoints` and tested on exact lines;
  - the essential out-of-memory message content.

### Batch 6 — the upscaler owns its contract

`components/rtxvulkan/upscale/*`, `vulkanrenderer.cpp`, `frametracer.cpp`, `trace/tracechain.cpp`, `device/driverprofile.hpp`

- [ ] **DLSS-RR traces at NGX's extents.**
  - Where: `vulkanrenderer.cpp:281` (`extentsFor`), `ngx.cpp:299`/`ngx.hpp:387` (`optimalSettings`,
    never called).
  - Defect: RR traces at FSR's ratios. The DLSS-RR guide (§3.2) and the DLSS Programming Guide
    (§3.2.1) require the optimal settings as given, and RR has no dynamic resolution (§3.3). A library
    that enforces the range fails the evaluation, and `dlssupscaler.cpp:608` ends the game.
  - Target: the `Upscaler` entity answers a mode's extents.
- [ ] **The half extent is spelled once.**
  - Where: `fsrupscaler.cpp:743` vs `:383-385`.
  - Defect: at a traced side of 1 the change pass dispatches 0 groups, and reactivity reads an
    unwritten arena.
  - Target: dispatch over `halfOf(render)`.
  - Test: a determinism pair at the smallest frame.
- [ ] **DLSS reports the layout it left.**
  - Where: `dlssupscaler.cpp:576`.
  - Defect: RR leaves the trace colour in `SHADER_READ_ONLY` while the hand-off says GENERAL;
    `RendererProbe::readComposite` would trip validation.
  - Target: an in/out `HandedImage&`, or a transition back.
  - Test: read the composite after an RR frame.
- [ ] **One answer to "what moved".**
  - Where: `tracechain.cpp:96`, `fsrupscaler.cpp:508`, `vulkanrenderer.cpp:287`, `frametracer.cpp:41,63`.
  - Defect: five places decide whether the extent moved.
  - Target: the one `Moves` from `movesTo`, handed to each resize.
- [ ] **FSR lays out at first record.**
  - Where: `fsrupscaler.cpp:526`.
  - Defect: a blocking `submitAndWait` at resize, then a clear again on reset.
  - Target: lay out at the first record, as `GBuffer` does.
- [ ] **One route for per-frame constants.**
  - Where: `fsrupscaler.hpp:114`, `dlssupscaler.cpp:356`.
  - Defect: a `PerSlot<Buffer>` per upscaler, where the visibility pass keeps one queue-ordered buffer.
  - Target: one buffer, as the visibility pass does.
- [ ] **Capabilities, not vendors.**
  - Where: `driverprofile.hpp:24`; `vulkanrenderer.cpp:124,158,175`; `dlssupscaler.cpp:387`.
  - Defect: a public `mVendor`, and vendor branches outside the vendor entity.
  - Target: `DriverProfile` states capabilities, and the NVIDIA upscaler entity answers its extensions
    and availability.
- [ ] **Tests for this batch:** an RR mode switch and a cut (on NVIDIA).

### Batch 7 — game side of the seam and the harness

`apps/openmw/mwrender/rtx/*`, `apps/openmw/mwrender/framedescriber.hpp`, `components/rtx/mirror/sceneextractor.cpp`, `apps/rtxtool/{main.cpp,toolline.hpp,framed.cpp}`

- [ ] **The cache clear runs in either order.**
  - Where: `rtxrenderer.cpp:672-693`.
  - Defect: `clearCache()` runs only when preparation outlives the kernels, so which models a run
    keeps depends on thread timing. Confirmed.
  - Target: clear after `finishPreparing` unconditionally.
  - Test: both orders.
- [ ] **Settings apply at the next frame.**
  - Where: `rtxrenderer.cpp:772,794-840`.
  - Defect: `applyChangedSettings`/`setVSync` are `noexcept` but reach target and swapchain rebuilds
    that throw, so out-of-memory terminates. Confirmed.
  - Target: every setting deferred to the next `renderFrame`, as `upscale` is.
- [ ] **One phase rule for world events.**
  - Where: `rtxrenderer.cpp:426,431,886`.
  - Defect: `addCell`/`removeCell`/`notifyCut` allow `Between` only, the emitters `Between` or `Run`.
  - Target: one rule.
- [ ] **Jumps by identity.**
  - Where: `framedescriber.hpp` (`mJumped`), `sceneextractor.cpp:164`.
  - Defect: raw node addresses across a frame, and a linear find per node walked.
  - Target: `StableIdentity`, sorted.
- [ ] **No dangling interface pointer.**
  - Where: `rtxrenderer.hpp:362` (`mGui`), `:865`.
  - Defect: `mGui` dangles from `~WindowManager`, and `freezeFrame` lacks the assert the others have.
  - Target: a detach call, and the assert.
- [ ] **The content is handed, not pulled.**
  - Where: `rtxrenderer.cpp:649,473`.
  - Defect: the renderer reaches `MWBase::Environment` for the store and the window manager.
  - Target: `prepareContent(const ESMStore&, Listener&)` from `Engine`; F3/F4 answered through
    `support()`.
- [ ] **The model list is derived.**
  - Where: `contentmodels.cpp:79-120`.
  - Defect: a hand copy of the model rules (`x`, `_sh`, shields), with ESM4 absent.
  - Target: derive the list from the `MWClass` model answers.
- [ ] **The harness honours the player's upscaler.**
  - Where: `apps/rtxtool/main.cpp:172`, `toolline.hpp:80`.
  - Defect: `view` ignores `[RTX] upscaler`, so FSR always runs. Confirmed.
  - Target, folded into the next item: the upscaler follows the same "named on the line" rule as every
    field.
- [ ] **The harness chooses its settings source once.**
  - Where: `main.cpp:155-202`.
  - Defect: three hand lists, string-keyed shipped reads, three spellings of "named", global writes, and
    no test.
  - Target: `RtxSettingValues::fromShipped` beside `fromRegistry`, a source chosen once, `std::optional`
    overrides merged in one place, all in `framed.cpp` with tests.
- [ ] **Idiom fixes:**
  - `lowestpoints.hpp:13` keys by `const void*`; key by `const MWPhysics::Object*`.
  - `sdlcursormanager.hpp:64` holds a raw `SDL_Cursor*`; use a `unique_ptr` with a deleter.
  - `renderer.hpp:556` holds the screenshot writer in a `ref_ptr`; use a reference.
- [ ] **Tests for this batch:**
  - `TracedGround::blacklistReference`'s moved-away rule;
  - `RippleEmitters`;
  - the `FrameDescriber` jump list;
  - `askedFor`/`askedValidation`/`askedBackend`/`playedRunSetup` (`rtxrenderer.cpp:111-186`);
  - `watchToItsEnd`/`submitDeferredWatched` (`commands.cpp:37-108`).

### Batch 8 — occupancy and shader correctness

`components/rtxvulkan/shaders/**`, `device/device.{hpp,cpp}`, `apps/components_tests/rtxvulkan/trace/visibility/kernels.cpp`

Measured heads (registers): panes 128–168 with 80–144 B spilled; visibility, landing, direct, media
and water 128; bounce 80–124; fsr accumulate 96; accumulate-clamp 59–66 with 12.7–14.7 KB shared;
atrous wide 60–68 with 9 KB shared.

- [ ] **The spill gate sees NVIDIA.**
  - Where: `device.hpp:32`, `kernels.cpp:166`.
  - Defect: NVIDIA's `Local Memory Size` is 2^36 + spilled bytes (the log reads `68719476752` for
    accumulate), and `mScratchBytes` is empty there, so the panes spills pass unflagged.
  - Target: read the low bits, and ceiling them as AMD's scratch is.
- [ ] **64-lane trace workgroups.**
  - Where: `shared/traceconstants.h:49-55`.
  - Defect: one-warp workgroups hit the per-SM block cap: 16 on Turing and Ampere, 24 on Ada (CUDA
    Programming Guide, compute-capability table). At ≤128 registers a Turing or Ampere SM holds 16
    warps whatever the count.
  - Target: `local_size_x = 64`, two tiles a workgroup, the odd tail subgroup returning.
  - Measure: `./omw release bench` A/B on the trace zones, and a Turing or Ampere card through
    `./omw remote`.
- [ ] **Shared tiles in their source encodings.**
  - Where: `trace/denoise/accumulateclamp.comp:106-114`, `atrouslevel.glsl:154-157`,
    `historyclamp.comp:55-56`, `shadowfilter.comp:76`.
  - Defect: tiles hold expanded `vec3`/`vec4` (144 × 88 B = 12672 B), so clamp runs at 14 warps where
    registers allow 28, and atrous at 20 against 32.
  - Target: the normal code, the distance, the RGB9E5 word, `packHalf2x16`: 88 → 32 B a texel for
    clamp, 64 → 24 for atrous. Bit-exact; `./omw kernels --against` shows no moved picture.
- [ ] **No runtime-indexed `vec4` in accumulate.**
  - Where: `lib/surfacematch.glsl:215-228` via `accumulate.comp:238,254`, and `:118-138`.
  - Defect: `shares[historyCorner]` is indexed at run time, spilling 16 B to local memory.
  - Target: `[[unroll]]` or component selects, checked with the spill bytes above.
- [ ] **A ray's request is not carried through the loop.**
  - Where: `lib/shading.glsl:595-601,724-746,952-1000`, `trace/direct.comp:66-89`, `traversal.glsl:425-441`.
  - Defect: `PathEndRays` (4 × 17 dwords) and `SolidLight` (3 × 17) stay live across every
    `rayQueryProceedEXT`.
  - Target:
    - regenerate each request in its turn;
    - share the origin as point + step + signs;
    - one traced mask;
    - in direct, fold `mRest * through` and the transmittance before the loop.
  - Estimated −15 to −25 registers. Confirm the peak in Nsight first.
- [ ] **The relief marches share a fetch and run first.**
  - Where: `lib/shading.glsl:1052-1060`, `lib/texturing.glsl:529`.
  - Defect: `reliefLit` fetches the same start height twice, after 51 dwords of requests are already
    live.
  - Target: fetch `start` once in `ReliefSite`, and run the relief before the requests are built.
- [ ] **Visibility stores before fog and sprites.**
  - Where: `trace/visibility.comp:277-417`.
  - Defect: `Answer` (~25 dwords) stays live through `fogAlong` and `spritesAlong`.
  - Target: store the independent channels before them.
- [ ] **Panes carries less through traversal.**
  - Where: `trace/panes.comp:45,61,94,97,101,177-189`, `lib/peel.glsl:48-69`.
  - Defect: two `PaneStack`s, an unpacked `StageRecord` and the unread motion z stay live through every
    traversal.
  - Target:
    - re-load the record after the loop;
    - write the surface and motion at layer 0;
    - keep `endMotion.xy` only;
    - measure an "eye under water" specialization that drops the column walk from above-water pipelines.
  - Folding the arms stack into the world stack is not bit-exact and needs an A/B.
- [ ] **Bounce keeps its record packed.**
  - Where: `trace/bounce.comp:80,100,112`.
  - Defect: the whole unpacked `StageRecord` is carried across the trace to rewrite one bit.
  - Target: `imageAtomicOr` on the packed word. This pays only with the 64-lane workgroups.
- [ ] **No NaN from a terrain normal.**
  - Where: `lib/traversal.glsl:1359`.
  - Defect: `normalize(across − normal·dot(normal, across))` with `across = (1,0,0)` is NaN for a
    LAND normal along ±x, and it poisons every history.
  - Target: a fallback axis, as `:1306` has.
- [ ] **Non-finite values never enter a history.**
  - Where: `accumulate.comp:304` and the history writes.
  - Defect: one non-finite sample enters the slow mean through `mix`, and the wavelet spreads it every
    frame. Only the harness census guards it.
  - Target: one select-to-zero at each history write, a value selected without a branch per AGENTS'
    one-path rule, with the census still counting.
- [ ] **The à-trous edge stop states its departure, or matches SVGF.**
  - Where: `trace/denoise/atrouslevel.glsl` (`spread = σ·sqrt(0.5(noise + tapVariance))`).
  - Defect: SVGF Eq. 5 (Schied et al. 2017) and ReLAX scale by the centre's variance alone. A noisy tap
    widens its own acceptance, so fireflies bleed. No comment says why.
  - Target: A/B with `./omw release noise --ab`, then keep the better rule with its measurement written
    beside it.
- [ ] **The register-reduction work comes last.** It follows the workgroup and shared-tile items,
  because those move the occupancy step every register count is judged against.

### Batch 9 — denoiser host structure

`components/rtxvulkan/denoise/*`, `trace/ripplepass.cpp`, `trace/tracechain.*`, `frame/gbuffer.cpp`

- [ ] **One temporal-layer pass.**
  - Where: `specularpass.*`, `panepass.*`, `denoisehistory.hpp:210,222`.
  - Defect: identical image types and pass bodies.
  - Target: one `LayerHistoryImages` and one temporal-layer pass from a constexpr row per layer.
    `DenoisePasses` calls `mHistoryClamp.record` directly.
- [ ] **Pipelines by switch from one table.**
  - Where: `accumulatepass.cpp:54,101`, `atrouspass.cpp:104,180`.
  - Defect: the variants are members chosen by nested ternaries.
  - Target: one `SpecializedPipelines<Key>`, as `ShadowPass` keeps.
- [ ] **No nullable pointer selecting behaviour.**
  - Where: `denoisepasses.hpp:57`, `atrouspass.hpp` (`const Composing*`).
  - Defect: a nullable selector, and a `record` of 10 parameters with 3 bools.
  - Target: two entry points or a `std::variant<Denoised, Composed>`, with `TraceSubject` passed whole.
- [ ] **Docs match the bindings.**
  - Where: `denoisepasses.hpp:44-46`, `atrouspass.hpp:66`, `frame/gbuffer.cpp:463`.
  - Defect: they say the cascade writes over `CHANNEL_DIRECT`; the shaders write `mComposed`.
  - Target: docs that name `mComposed`.
- [ ] **One "no-shadow" stand-in.**
  - Where: `atrouspass.cpp:109`, `compositepass.cpp:38`.
  - Defect: two identical images.
  - Target: one, owned by `TracePasses`.
- [ ] **Ripple candidates never reallocate.**
  - Where: `trace/ripplepass.cpp:86-87,189-191`.
  - Defect: filled past the reserve, then trimmed, so a crowded frame reallocates.
  - Target: a fixed-capacity max-heap.
- [ ] **Split asserts, one ownership shape.**
  - Where: the passes' size asserts, `denoisepasses.cpp:85`, `tracechain.hpp:139,156`.
  - Defect: compound asserts don't name the broken half. `TraceChain` holds `GBuffer` and `FogVolume`
    in `unique_ptr` but `DenoiseHistory` in `optional`.
  - Target: split asserts, and `optional` with `emplace` for all three.
- [ ] **Tests for this batch:**
  - the cascade's compose and the composite's give one picture (`mAccumulate` 0 vs 1, frame 0);
  - a table-driven test of `DenoiseHistory::discard` per (role, pair, fresh, runs).

### Batch 10 — the suite's speed and determinism

`tools/omw/testing.py`, `apps/components_tests/**`, `apps/openmw_tests/**`

Measured: the suite takes 86 s. The GPU shards take 84–86 s each in parallel, and `components` 38 s,
of which 25 s is upstream's detournavigator. Tests over 1 s:

| test | time |
|---|---|
| `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` | 29.7 s |
| `RtxVisibilityKernelsTest.tuplesAStageReadsAlikeShareItsPipeline` | 27.6 s |
| `RtxRayReconstructionTest.theFeatureIsFrameMemoryTheReserveKeepsRoomFor` | 2.1 s |
| `RtxBufferTest.uploadsLandWhereTheySay` | 1.2 s |
| `RtxVisibilityKernelsTest.theKernelsAreCountedAsTheyLandAndTheCountEndsWithTheCompile` | 1.1 s |
| `RtxBounceClampTest.theFloorFollowsASkyThatChangesHueAtOneLuminance` | 1.0 s |

- [ ] **The kernel tests compile each tuple once.**
  - Where: `rtxvulkan/trace/visibility/kernels.cpp`.
  - Defect: two tests each compile every pipeline tuple: 57 s together.
  - Target: compile each tuple once per binary, in a shared fixture, and let both tests read it. The
    spill test reads the statistics from batch 8.
- [ ] **The gate enforces the user's limits.**
  - Where: `tools/omw/testing.py:50`.
  - Defect: `LIMIT_SECONDS = 2.0`, no suite bound, and a suite with no report passes.
  - Target: 1 s a test, 30 s a suite, and a failure when a suite that ran wrote no report.
- [ ] **A failed assert leaks no renderer state.**
  - Where: `rtxvulkan/support/device/harness.cpp` (`RendererTest::SetUp`), `rtxvulkan/frames.cpp:694-704`.
  - Defect: the reset misses `mReservedUpscales` and the fog field, so a failed assert leaks the
    reserve into the rest of the shard.
  - Target: one restore of everything the renderer was made with, and RAII guards from the setters.
- [ ] **Every test loop has a bound.**
  - Where: `rtx/mirror/cells/cellring.cpp:498,606`, `rtxvulkan/support/device/harness.cpp:271`,
    `rtx/prepare/contentpreparation.cpp:144`, `rtx/common/job.cpp:57,102`,
    `rtxvulkan/trace/visibility/kernels.cpp:68`.
  - Defect: unbounded loops hang to the CTest timeout.
  - Target: a bound, then `FAIL()` naming what was pending.
- [ ] **No time in the assertions.**
  - Where: `misc/monitor.cpp:192` (250 ms sleep), `misc/frameclock.cpp:62` (wall clock),
    `rtxtool/perffifoposix.cpp:131`, `platform/thread.cpp:51`.
  - Defect: sleeps prove negatives, and wall-clock asserts fail under ASan, TSan or load.
  - Target: an injected clock, and a wake hook that counts.
- [ ] **Deterministic frames are asserted near their value.**
  - Where: `rtxvulkan/trace/visibility/sea.cpp:137,316,317,553,561,880,881`, `trace/wavepass.cpp:288`.
  - Defect: loose bounds on deterministic frames, with the measured value in the message.
  - Target: `EXPECT_NEAR` with its tolerance's reason written beside it.
- [ ] **One temporary folder per test.**
  - Where: `platform/appendfile.cpp:24`, `rtxtool/instruments/amdgpulinux.cpp:22` and others.
  - Defect: three temp-file conventions, and an early `ASSERT` leaves files behind.
  - Target: one RAII per-test folder in `rtx/support`.
- [ ] **The GPU suite owns its pipeline cache.**
  - Where: `getPipelineCacheSpec`.
  - Defect: the GPU suite reads the user's real cache.
  - Target: a cache of the suite's own, so compile time and what is compiled do not depend on the home
    folder.
- [ ] **Generated tables are checked by the build.**
  - Where: `rtx/world/fogbuilder.cpp:218,319,345`, `rtx/frame/bluenoise.cpp`,
    `rtx/frame/specularalbedo.cpp`.
  - Defect: 0.3–1.0 s each re-running the table makers.
  - Target: "compiled table == maker" in the build or lint step, and sample sets made once per binary.
- [ ] **The store's fault tests share one store.**
  - Where: `contentstore/store.cpp:166`.
  - Defect: 10 cases each build two stores on disk.
  - Target: a table over one store.

### Proposals to upstream code (each waits for the user's yes)

- [ ] **Crash notes in the game's own work.**
  - `Engine::frame`, one per `ScopedProfile` block.
  - `SceneManager::getTemplate` ("loading the model \"{}\""), which covers every thread.
  - `ImageManager::getImage`.
  - `StateManager::loadGame`, plus the record type in its loop.
  - The ESM load loop, per content file.
  - `Scene::insertCell` per reference, `changeCellGrid`, `changeToInteriorCell`.
  - `CellPreloader::doWork`.
  - `AsyncNavMeshUpdater::processJob`.
  - `mwlua/worker.cpp`.
  - The `mtphysics` worker.
- [ ] **Annotations of where the player was:** the current cell (set in `Scene`), the save loaded
  (`StateManager::loadGame`), and the content-file count.
- [ ] **Reference roots named** with their RefId (`Objects::insertBegin`), or the model's VFS path on
  its root beside `sFileHash`. Batch 4's refusals take whichever is approved.
- [ ] **A ledger entry for the seam's restructure of upstream code.**
  - What it covers:
    - `Engine` loses `createWindow`, `osgViewer`, `Stereo::Manager`, `Resource::Profiler`,
      `StatsHandler` and `OPENMW_OSG_STATS_FILE`;
    - `World::init`, `RenderingManager`, `WindowManager`, `InputManager`, `ControllerManager` and
      `InputWrapper` take `Renderer` or `GraphicsListener`;
    - `CharacterPreview`, `LocalMap` and `GlobalMap` stand on `OffscreenView` and `MapOverlay`;
    - `setExteriorFlag` moves to `GlWorld`.
  - Its merge cost written with it.
- [ ] **A maximized window is not stored as its size.**
  - Where: `apps/openmw/mwgui/windowmanagerimp.cpp:1306-1317` (`windowSized`, from the SDL3 port).
  - Defect: a maximized window's size is stored as `[Video] window width/height`, so the next start
    opens an unmaximized window as large as the display.
  - Target: skip the store while `SDL_WINDOW_MAXIMIZED`, as fullscreen is skipped.
  - Test: fullscreen and maximized.
- [ ] **The monitor as the game's parent**, merged with the AppImage keeper: a design decision for
  the user (see the Proposal), after batch 2.
