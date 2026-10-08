# Review: the fork's diff against upstream — prerelease polish

Scope: `git diff 2f0688aa59 HEAD` (2f0688aa59 is the merge base with `upstream/master`). The review
covers the fork's own code and the fork's hunks in upstream files. It excludes test code and
`extern/fidelityfx`. Seven reviewers read the tree in parallel: the Vulkan device layer, the Vulkan
passes, the shaders, the mirror, the rest of the core, the game side with the upstream hunks, and
the harness with the crash catcher, `./omw` and CI. A second pass checked each target shape against
the code and made it structural where the first pass patched one site.

Whoever addresses an item deletes it. When a phase is empty, delete its heading. The items are in
plan order: a later item can depend on an earlier one, and the dependency is named.

No item changes behaviour on a valid path, except item 3.12, which the user approved. Each item
names how to verify it. Every item ends with the usual chain: `./omw build`, the named test filter,
and at the end of each phase `./omw test` and `./omw gate`.

## Before the first item

Take the baselines once, outside `/tmp`, whose cleaner empties it under a long session:

- `./omw shot --views=all --map --upscale=off --out=<baseline>/shots`
- `./omw kernels > <baseline>/kernels.txt`
- `./omw release bench > <baseline>/bench.txt`, after a throwaway warm-up leg, on a quiet desktop,
  in the background.

At the end of every phase, `shot --against` and `kernels --against` name nothing, except what an
item names: 3.5 adds the `SHADOW_LEVEL` tuples (take a fresh `kernels.txt` after it), and 3.12 moves
the `HAS_MAPS=1` tuples and `normalspread`. Phase 4 quotes the bench's median, p99 and worst frame
against `bench.txt`.

## Verdict

Resource ownership is structural almost everywhere. Every Vulkan handle the passes hold is an
`Owned`, `Image`, `Buffer` or `AccelerationStructure`, buried in the graveyard under the next
submit's timeline value. No `VkShaderModule` exists (code goes inline through `maintenance5`). The
mirror keys every identity by `osg::ref_ptr`, so no entry can dangle. The reviewers found no data
race and no leak in normal use.

What is left:

- **One live bug.** `BloomPass::record` opens a GPU timer zone before an early return. Together
  with `VK_QUERY_RESULT_WAIT_BIT` in `GpuTimer::resolve`, a frame under 8 px on a side can hang the
  queue read (items 1.1 and 1.2).
- **Error paths that leak or destroy garbage.** These run only when a `vkCreate*`, a constructor or
  a write fails (phase 1).
- **Manual pairings a reader checks by eye.** Timer zones, world attachment, command recordings,
  the instance, the crash page, the GL window (phases 1 and 2).
- **C++ and shader facts that nothing checks against each other.** Descriptor sets 1–3,
  specialization IDs, push block sizes, vertex locations, the CMake shader list (phase 3).
- **Frame-path costs in the mirror.** Unloaded cells are deleted on the frame thread (phase 4).
- **Simplifications and dead code** (phases 5 and 6).

## Phase 5 — design and simplification

### Harness, `./omw` and CI

- [ ] **5.22 `./omw` exit statuses.**
  `tools/omw/main.py:72,78`, `testing.py:45`, `game.py:268` return a negative `returncode`, so
  SIGSEGV exits 245, not 139. `main.py:145-146`: `omw help` exits 2. Target: one `status(code)`
  helper in `system.py`, and `help` returns 0. Update `test_main.ParseTest`.

- [ ] **5.23 `omw profile --offcpu` does not own its children and drops perf's status.**
  `tools/omw/perf.py:84-88,242-252`. `omw profile | head` leaves the harness running, and a refused
  perf shows only the harness's 30 s fifo timeout. Target: `with Popen(...)` for both, a `finally`
  that kills a live harness, and perf's status reported.

- [ ] **5.24 `kernels.py` error handling.**
  `:178-179`: a refusal in one worker waits for every other tuple. Use
  `shutdown(cancel_futures=True)`. `:170`: `spirv-dis` with `check=True` loses its stderr. Use the
  same `Refusal` shape as `spirv-opt`.

- [ ] **5.25 `processposix.cpp` names `WIFSIGNALED`/`WEXITSTATUS` without `<sys/wait.h>`.**
  `components/platform/processposix.cpp:216-226`.

- [ ] **5.26 CI repeats the package job and the log upload.**
  `.github/workflows/daily.yml:37-60` and `rtx-release.yml:222-270` are one job. `ci.yml:195-206` and
  `sanitizers.yml:174-182` are one upload. Target: a reusable `package.yml` and a composite
  `build-logs` action. Make the Linux and Windows deps cache keys agree on `deps.py`
  (`actions/openmw-deps/action.yml:294,306`). Keep the required check names.
  Verify: zizmor in the checks job, a manual `rtx-release.yml` and `daily.yml` run.

### Device layer

- [ ] **5.27 Small device items.**
  - `device/instance.cpp:48,78-88`: `loaderOffers` enumerates the instance extensions on each of
    three calls. Enumerate once.
  - `device/handles.hpp:34`: `makeTimelineSemaphore` has one caller and belongs in `timeline.cpp`.
  - `device/memory/growablebuffer.hpp:23-31,49`: the name is a `std::string_view` that is "a
    literal" only by comment. Take a `const char*`.
  - `device/retiring.hpp:11-15`: the comment omits `StructureStorage::mCooling`.

## Phase 6 — dead code and stale comments

- [ ] **6.1 The empty sprite list in `TraceMedia` is dead.**
  `trace/tracemedia.hpp:52-54,96-98`, `tracemedia.cpp:18-22`. Commit `b569c7484a` removed its only
  caller, and a host-visible buffer is still allocated. Delete it, then `Buffer::zeroOnHost`
  (`device/memory/buffer.hpp:215`), which loses its last caller. Rewrite the four comments that
  describe it: `trace/visibilitypass.hpp:59-61`, `trace/tracemedia.hpp:28`,
  `components/rtx/shaders/scene.h:1009-1012`, `shaders/lib/spritelist.glsl:113-114`.

- [ ] **6.2 Members with no caller.**
  `Presenter::getExtent` (`present/presenter.hpp:63`, `.cpp:162-165`),
  `RtxRenderer::getBackend` (`mwrender/rtx/rtxrenderer.hpp:203-206`),
  `SlotPool::getSlots` (`common/slots.hpp:62-64`).

- [ ] **6.3 Core nits.**
  - `SceneDesc::forEachPlacement` (`scenedesc.hpp:269`) has one caller. Inline it.
  - `mSeaHeading` is written in `frame/camera.cpp:63` and `world/frameworld.cpp:195`. Keep one.
  - `noDeck`/`noStars`/`noPatch` belong in `skycontent.hpp`, not `frameworld.hpp`.
  - The include blocks of `world/{atmosphere,fogbuilder,frameworld,skylight,weather}.cpp` lack the
    blank line after the own header.
  - `renderer/sceneuploader.hpp:16` forward-declares `Renderer` after it includes `renderer.hpp`.
  - `environment/cloudmesh.cpp` has two adjacent anonymous namespaces.
  - `platform/fifowin32.cpp` `write` throws where its comment says "Never reached". Make it a
    `Crash::fatal`.

## Documents to update with the items

- `docs/rtx/architecture.md`: the seam's attachment and the unref queue it carries (1.11, 4.1), the
  command pool's model of lent buffers, recordings and batches (2.1 to 2.3), the pipeline cache's
  file format (1.8), and the one module interface reader (3.1).
- `AGENTS.md`: the fourth approved fault (5.14). Item 5.13 removes a fork change, so nothing is
  added for it. The `kernels` claim holds again after 3.5.

## Checked and found sound

The reviewers checked these and found nothing to change. The list keeps a later review from
repeating the work.

- **Graveyard and timeline.** One queue in stamp order, burial order is free order, every wait
  collects, `ReadStamp` is correct.
- **Teardown order.** `~Device` waits idle first, the graveyard goes before the timeline and the
  allocator, the `VkDevice` last. `VulkanRenderer` declares instance, surface, device in order.
- **Swapchain.** Per-image present semaphores, an acquire ring reused only after its blit passed
  (no VUID 01779 reuse), one `mStale` flag, a hidden window kept stale without a rebuild.
- **Acceleration structures.** Rooms cool on the timeline, compaction is budgeted and survives a
  replaced query pool, the top level doubles past 2^18 rows, retirement is by burial.
- **Descriptors.** Bindless sets per slot with update-after-bind, owed slots written after the
  set's last bind. Every other set is a checked push descriptor.
- **Barriers.** Every chain in trace, denoise, display, upscale, texture arrival, GUI and present.
  The head barrier serialises the frames. Validation tracks layouts at submit, so no render graph is
  proposed.
- **Pipeline cache.** Bounded size, header and UUID checked, shader digest in the key, atomic rename,
  partial files swept (save item 1.8).
- **Shaders.** Set and set-0 binding numbers, storage formats, workgroup sizes and spec IDs are
  single-sourced. Every shared struct is `scalar` with asserted sizes. `nonuniformEXT` on every
  bindless index. NaN and infinity are preserved by the pinning and counted by the census. No dead
  shader code and no duplicated GLSL function.
- **Mirror.** Identity maps keyed by `ref_ptr`, symmetric holds, bounded tables, no `std::function`,
  no observers, steady frames covered by allocation tests.
- **Core.** No Vulkan include, folder order held, symmetric add and drop in all five tables,
  untrusted image data refused at the boundary, threads joined and exceptions carried out.
- **Game side.** Only `engine.cpp` and `renderer.cpp` name `RendererKind` or `RtxRenderer`.
  `RtxRenderer` member order is correct. `TracedView`, `TracedOverlay` and `TextureHandle` register
  and give back symmetrically. No frame-path allocation. The sampled upstream hunks are each an
  accepted item, a warning fix, an SDL3 rename or seam reuse, save items 1.13, 5.13 and 5.14.
- **Harness and crash catcher.** Every descriptor this code opens is close-on-exec. The crash path
  uses only async-signal-safe calls. The hang watch threads are joined. Untrusted config is refused
  with file and line. Options are parsed once. CI actions are pinned to commits.

## Declined while merging

- **Freezing the frame in the backend** (`RtxRenderer::freezeFrame`, `rtxrenderer.cpp:636-661`). It
  reads the frame back and uploads it again, once per loading screen. Pursue it only if that hitch
  measures.
- **A surface-lost path in the presenter.** It surfaces as `DeviceError` today, which fits the target
  desktops.

## Phase 7 — after every other item: investigate until clear

Start this phase only when every item above is done. Find the cause first, then fix it at the
owner. Do not fix by guess.

- [ ] **7.1 Two runs of one binary do not agree at `seyda-neen-ship-armed`.**
  Reproducer, about one minute:
  `./omw shot --views=probe-guild-planter,seyda-neen-ship-armed --upscale=off --filter=false --out=<a>`,
  then the same with `--against=<a>`. Nearly every pair differs in the armed stop's scene digest:
  `meshes`, `instances`, `previous` and `poses`, and on some pairs also `positions`, `normals`,
  `texcoords`, `colours` and `indices`. With `--views=all`, only some pairs differ. A 0.5 ms busy
  wait per placement makes it more frequent. The issue log said that the place is a function of the
  frame time and that two runs agree. Both claims are wrong: it is a race.

  What differs, found by temporary dumps (all reverted):
  - The actors after the teleport into Seyda Neen. Other mudcrabs stand, at other positions and in
    another count (`MechanicsManager::getActorsInRange`, `RefData::getPosition`).
  - Standing actors' body parts are one ulp apart in z. The player's own body parts differ too.
  - The world generator (`World::getPrng`, hashed after each walk) is equal through the first frame
    after the teleport and differs from the second frame. In some runs the poses and the
    placements differ already on the first frame.
  - The engine's frame number at the first traced frame differs between runs (167 against 189, 213
    against 140): the start-up's loading frames are counted by the wall.

  Ruled out, each tested with the reproducer or with the full suite:
  - the measure window's wait: the same "stood whole" frame in both runs;
  - the physics: `dt` is nought on every frame, the accumulator is the same in both runs, and
    ignoring the step budget (`PhysicsTaskScheduler::calculateStepConfig`) changes nothing;
  - the Lua worker: `lua num threads = 0` still differs;
  - the cell preloader: `preload enabled = false` still differs;
  - the top-level room, and the light grid's history (built from nothing on every frame);
  - the seed's place alone: `Stager::stage` seeds both generators after `world.changeToCell`
    (`apps/rtxtool/stager.cpp:100` against `:137-138`), so the cell load's levelled spawns
    (`CreatureLevList::insertObjectRendering`) draw from the stream the frames before left. Seeding
    ahead of the move did not settle it. Keep it in mind for the fix all the same.

  Next steps:
  - Find every draw from `World::getPrng` between the first and the second frame after the
    teleport, with its call site, in two runs, and diff the lists: a counting wrapper on the
    generator, or a breakpoint with a backtrace. The draws seen in the code are `AiWander`
    (`aiwander.cpp:68,402`), `CharacterController` (`character.cpp:323,806`), `NpcAnimation`'s blink
    (`npcanimation.cpp:159`), `WeatherManager` (`weather.cpp:169,784`), the levelled spawns
    (`creaturelevlist.cpp:112`) and Lua's nearby bindings (`mwlua/nearbybindings.cpp`).
  - Find the threads that can touch the world's state in that window: the Lua worker runs beside
    `renderFrame` (`engine.cpp:294-298`), the navmesh updater, and the ray tracer's cell ring reader.
  - `Misc::Rng`'s default generator is one static that every thread shares (`misc/rng.cpp:9`); the
    lamp flicker (`lightcontroller.cpp:16,64`), the NIF particles (`nifosg/particle.cpp`) and the
    loading screen's splash draw from it.

  Verify: five pairs of the reproducer and five pairs of `--views=all --filter=false` agree in every
  column, and `./omw repeat --pairs=10`.

- [ ] **7.2 With the denoiser on, the trace's direct light moves between two runs of one binary.**
  `./omw shot --views=all --upscale=off` twice: `g-direct`, a trace channel, differs at up to 30
  places, from some stop on through the rest of the run, and nothing else differs. With
  `--filter=false`, no place differs (apart from 7.1). So the trace reads something the denoiser
  wrote, and the card's known one-ulp difference in the wavelet (AGENTS.md, "The denoised frame is
  not bit-exact on this card") reaches the trace and the shot's verdict, which then names the trace.
  Find what the trace reads of the denoiser's output, and whether it should.
