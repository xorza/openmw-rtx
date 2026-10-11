# Pre-release audit: plan

The audit read the fork's whole diff against `upstream/master` (merge base `56670e4e95`, 1873
commits, 1663 files). Nine read-only passes covered the Vulkan device layer, the frame passes, the
shaders, the scene mirror, the core modules and MyGUI backend, the renderer seam and the engine, the
fork's hunks in upstream game code, the platform, crash catcher, build and CI, and the harness.
`extern/fidelityfx` is vendored and unchanged, so it was not read.

The audit found 52 faults. Faults with one cause or one fix share an item, so the plan has 32 items.
These areas were traced and held: the barriers, slot stamps, acceleration structures, texture
upload, the upscaler, the display chain, the shared struct layouts, the bindless indexing, the SDL3
return conventions, the presentation math, the visibility gates' threading and the warning-clean
hunks.

## How to read an item

- **Verdict**: CONFIRMED means the failing path was traced in the code. PLAUSIBLE means one point is
  open, and the item's **Close first** line says how to close it before the fix. When the point
  closes the other way, delete the item.
- **Upstream** marks an item that changes an upstream file. [Decision 1](#decisions) says which
  AGENTS.md line each one gets.
- The audit IDs (`SEAM-1` and the like) name the auditors' notes. They are not in the code.
- **Verify** each item as AGENTS.md says: build the touched targets, then `./omw test <binary>
  --gtest_filter=...`. For a change that a frame reads, also `./omw repeat --pairs=10` and `./omw shot
  --views=all --map --upscale=off --against=<baseline>`. For a shader change, also `./omw kernels
  --against`. Run `./omw gate` once at the end.
- A fix that this plan leaves for later goes to `.notes/ISSUES.md` in the same commit, as the
  issue only.

## Order

The items are in priority order, and they can be done in that order. Four links between them:

- Item 6 refuses a frame past the device's limit with an error that `renderFrame` throws. Item 3
  makes that throw reach the error box, so do item 3 first.
- Items 6 and 7 read one device limit. Expose it once, in item 6.
- Item 14 moves the GUI's texture reader onto the core's. Item 13 changes the core's reader. Do item
  13 first, and item 14 then carries the X8 rule into the GUI with no code of its own.
- Items 8 and 9 change the same reader and kernel. Do item 9 first: it refuses the skins that item
  8's test must not meet.

## Decisions

The user took these calls on 2026-10-11. The items carry them.

1. **Upstream files.** Five items change an upstream file as an improvement to the ray tracer's
   integration. Each one gets its line in AGENTS.md's Accepted diff in the same commit as the code:
   - item 3, the order in `~Engine`: a new line;
   - item 4, `EngineHost::getSavesFolder`: added to the existing `OMW::EngineHost` line;
   - item 15, the stamp on an embedded texture in `NifOsg::Loader`: a new line;
   - item 31, F2: added to the existing `Renderer::support` line;
   - item 32, the `StableIdentity` stamps that `Objects` puts on cell roots and references: a new
     line, which also covers the stamps already in the tree.
2. **The MSVC runtime (item 2):** app-local DLLs from the build's own toolset.
3. **Embedded NIF textures (item 15):** traced under a key of their own.
4. **Large textures with no mips (item 21):** above vanilla's largest side, the completed chain is
   encoded to BC7.
5. **A failed start of the ray tracer (item 22):** a better message only. A probe when the box is
   ticked would start the renderer not chosen, which AGENTS.md forbids.

## P0: release blockers

## P1: crashes and wrong pictures

### 20. Stop the window size drift on a mixed-scale Wayland desktop
- **Skipped**: see `prerelease-audit_QUESTIONS.md`. The fault is confirmed in SDL's source, and no fix can be tested on one display.
- **Audit**: GAME-1. PLAUSIBLE: traced through SDL's Wayland backend, not run.
- **Close first**: Run the game windowed on two outputs at scales 1.0 and 2.0, and start it twice.
  The size in `settings.cfg` must change between the starts.
- **Where**: `apps/openmw/mwrender/renderer.cpp:395-409` (`openWindow`),
  `components/sdlutil/sdlvideowrapper.cpp:79-82`, `apps/openmw/mwgui/windowmanagerimp.cpp:1306-1316`.
- **Problem**: `openWindow` divides the stored pixel size by the density of the hidden window. On
  Wayland, SDL gives an unmapped window the largest scale of any output. The window opens at half
  size on the 1.0 output, `windowResized` stores the half, and each start halves it again.
- **Fix**: Store no window size until the first density change after the window is mapped.
- **Test**: A pure helper that takes the density at creation and after mapping: a stored size goes
  through unchanged.

## P2: hardening

### 25. Keep the player's name out of the prefilled crash issue
- **Audit**: BUILD-3. CONFIRMED.
- **Where**: `components/crashcatcher/crashsummary.cpp:150`, `crashpackage.cpp:448-475` (`newIssueUrl`).
- **Problem**: The summary's last line is the dump's absolute path, which holds the account name. The
  public issue's body carries it.
- **Fix**: `newIssueUrl` leaves out the dump line. The log and the package keep the full path.
- **Test**: `CrashPackage`: `newIssueUrl` over a summary with `/home/someone/...` has no `someone`.

### 26. Make the crash catcher robust on each platform
- **Audit**: BUILD-4 (PLAUSIBLE), BUILD-5, BUILD-6 (CONFIRMED), BUILD-7 (PLAUSIBLE).
- **Close first**: BUILD-4: on Windows, open a 6000-character URL with `SDL_OpenURL`. BUILD-7 is a
  fault of the design (a predictable name opened without `O_EXCL`) whatever the system's settings,
  so it needs no close.
- **Where**: `components/crashcatcher/crashpackage.cpp:41-43`, `crashpadmonitor.cpp:698-700`,
  `components/platform/processwin32.cpp:127-145`, `components/crashcatcher/crashpadclient.cpp:131-143`,
  `components/platform/sharedmemoryposix.cpp:22-44`.
- **Problem**:
  - On Windows, a new-issue URL past about 2083 characters may not open, and the result of
    `SDL_OpenURL` is ignored.
  - On Windows, `runShell` gives UTF-8 to the ANSI `std::system`, so a non-ASCII path arrives
    corrupted. `shellWord` lets a trailing backslash escape its closing quote.
  - On Linux, `StartHandler` succeeds once the fork succeeds. A monitor that dies at a read-only
    report folder leaves the game with no catcher, a log that says otherwise, and a page in `/dev/shm`
    until reboot.
  - The shared page's name is predictable, and `create` opens a page that another user made.
- **Fix**: Check `SDL_OpenURL`, and on failure open the bare `/issues/new?title=` URL. On Windows,
  widen to UTF-16 and use `_wsystem`, and double a trailing run of backslashes. After `StartHandler`,
  wait for a bounded time for a ready word in the page, and report a monitor that did not start; the
  game then unlinks the page. Create the page with `O_EXCL`: unlink and retry a stale page of the
  player's own, and refuse any other.
- **Test**: `ProcessWin32` with a word that holds `ö` and a word that ends in `\`. The crash matrix
  with a read-only `OPENMW_CRASH_REPORTS`: `install` fails and no page stays. `SharedMemory`: a name
  made first with another mode is not mapped.

### 27. Make a failed or cut-short harness run fail
- **Audit**: HARNESS-2, HARNESS-4, HARNESS-5, HARNESS-7. CONFIRMED.
- **Where**: `apps/rtxtool/hosted.cpp:207-224`, `session.cpp:241-251`, `:469-490`,
  `stopwriter.cpp:503-521`, `measurer.cpp:85-86`, `instruments/frametimes.cpp:283-305`,
  `main.cpp:786`, `model/runrecord.cpp:47-88`.
- **Problem**:
  - A `bench` whose window closes before the last stop exits 0 with no `--json`, no hashes and no
    `--against` comparison.
  - `--doll` with an id that is not an NPC throws out of `Session::frame` and ends the run with no
    report.
  - `Engine::frame` catches an exception from `beginStop`, and the stop begins again every frame.
    `--perf-control` with no reader waits 30 s in the frame each time, with no end.
  - `hashes.csv` and `--json` from the last run stay until this run finishes, so a run that dies
    leaves another build's hashes beside its pictures.
- **Fix**: A session that ends with stops left calls `RunRecord::abandon`. `--doll` looks the id up
  in `ESM::NPC` first, and `fail()`s when it is not there. `Session::beforeFrame` catches and
  abandons. `PerfControl::open` runs before the engine starts. Remove `hashes.csv` and the `--json`
  path next to `clearPictures`.
- **Test**: `components_tests/rtxtool/runrecord.cpp`: a three-stop record with one place, described
  without `finish`, has a non-zero status. The `clearPictures` test also removes `hashes.csv`.

### 28. Make the harness's names and numbers safe
- **Audit**: HARNESS-3, HARNESS-6, HARNESS-9. CONFIRMED.
- **Where**: `apps/rtxtool/run.cpp:389-390`, `instruments/framehashes.cpp:212-213`, `:277-287`,
  `main.cpp:562-579`, `options.cpp:56`, `:211-218`, `model/benchspec.cpp:59-71`.
- **Problem**:
  - A stop named after its cell (`-2,-9`, `Balmora, Guild of Mages`) puts commas in `hashes.csv`, and
    the next `--against` cannot read it. A `:` in a picture's name writes an NTFS stream.
  - With `--views`, the options `--cell`, `--view`, `--pos` and `--look` are ignored with no word.
  - Frame counts from seconds wrap past `uint32`, and `--frames=4294967295` equals the "until closed"
    sentinel.
- **Fix**: Name a stop that has no view by `slugOf`, and have `FrameHashes::write` refuse a `,`.
  Refuse `--views` beside a place option. Bound `--frames` below `sUntilClosed`, and refuse a span
  whose count does not fit.
- **Test**: `rtxtool/run.cpp`: `stopFor` on cell `-2,-9` gives a name with no `,`, `:` or `/`.
  `framehashes.cpp`: a round trip. `options.cpp`: `shot --views=a --pos=1,2,3` and
  `--frames=4294967295` are refused. `benchspec.cpp`: 1e8 s at 1/60 is refused.

### 29. Watch the card the renderer runs on, check the harness's writes, and keep its keys out of text
- **Audit**: HARNESS-8, HARNESS-11 (PLAUSIBLE), HARNESS-12 (CONFIRMED).
- **Close first**: HARNESS-8 needs a machine with two cards, so the fix stands on the code alone.
  HARNESS-11: write a PNG to `/dev/full` and see whether the run says "wrote".
- **Where**: `apps/rtxtool/instruments/nvml.cpp:76`, `instruments/amdgpu.cpp:57-90`,
  `components/rtx/renderer/png.cpp:62-90`, `instruments/framehashes.cpp:227-230`,
  `model/benchrecord.cpp:557-560`, `noise.cpp:157-160`, `skykeys.cpp:9-23`, `homekey.cpp:45-55`,
  `memorykey.cpp:41-47`.
- **Problem**:
  - The card watch reads NVML index 0 or the first AMD card in `/sys`, not the Vulkan device. On a
    laptop with an APU and a discrete card, it can read the APU.
  - The PNG writer and the text writers do not check the close. A full disk or a CIFS mount reports
    "wrote" over a truncated file.
  - The sky, Home and End keys read the raw key state, so text typed in the console turns the sky.
- **Fix**: Pick the card by the renderer's PCI address (`VK_EXT_pci_bus_info`), and say "card not
  watched" when no card matches. Encode the PNG in memory, and write it with a checked `write` and
  `close`. Check the text writers after an explicit `close`. Skip the three key listeners while the
  console or text input is active.
- **Test**: `rtxtool/cardwatch.cpp`: `AmdGpu::find` over a fake sysfs with two AMD cards picks the
  asked PCI address. A write to `/dev/full` gives an error.

## P3: structure

### 30. Let the required formats table give its own length
- **Audit**: VKDEV-3. CONFIRMED.
- **Where**: `components/rtxvulkan/device/requirements.cpp:262`.
- **Problem**: `std::array<RequiredFormat, 4>` has three entries. The fourth asks every device about
  `VK_FORMAT_UNDEFINED`, with an empty reason.
- **Fix**: `constexpr std::array sRequiredFormats{ ... }`, as the other tables are.
- **Test**: No entry of `getRequiredFormats()` is `VK_FORMAT_UNDEFINED` or has an empty `mFor`.

### 31. Answer F2, F3 and F4 under the ray tracer through `Renderer::support`
- **Audit**: SEAM-5, SEAM-6. CONFIRMED. **Upstream** for F2 (decision 1, the `Renderer::support` line).
- **Where**: `apps/openmw/mwgui/windowmanagerimp.cpp:2410-2418`,
  `apps/openmw/mwrender/rtx/rtxsupport.cpp:29-31`, `:108`, `apps/openmw/mwrender/rtx/rtxrenderer.hpp`.
- **Problem**: F2 says that post-processing "is not enabled" when the setting is on and the ray
  tracer declines it. F3 and F4 do nothing, and `RenderSupport` does not declare the stats overlay.
- **Fix**: F2 asks `support().declinedSetting("Post Processing", "enabled")` and shows
  `MWRender::notAvailable` with that reason. `rtxSupport()` declares the stats overlay, and
  `RtxRenderer::functionKey` reports it once.
- **Test**: `RtxSupportTest`: the declaration names the overlay.

### 32. Make one helper the only maker of a cell root
- **Audit**: SEAM-8. CONFIRMED that the stamp is missing. No wrong picture was shown.
- **Where**: `apps/openmw/mwrender/objects.cpp:49-55`, `:209-221`. **Upstream** (decision 1, new line).
- **Problem**: `updatePtr` makes a cell root without the `StableIdentity` stamp and the "Cell Root"
  name, and `insertBegin` then uses it again. The mirror tells roots apart by the stamp.
- **Fix**: One private helper names and stamps a cell root, and `insertBegin` and `updatePtr` call it.
- **Test**: Move a Ptr into a cell with no node, and the new root carries a `StableIdentity`.
