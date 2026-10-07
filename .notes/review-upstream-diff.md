# Review: the fork's diff against upstream

Scope: `git diff 2f0688aa59 HEAD` (merge base with `upstream/master`), without tests and without
`extern/fidelityfx`. Whoever addresses an item deletes it. When a group is empty, delete its heading.

## The frame path allocates, copies, or rebuilds behind a threshold

- [ ] **Blocked: Q1 in `review-upstream-diff_QUESTIONS.md`.** `components/rtx/scene/lightgrid.cpp:83-86,102-106,151-165` — `rebuild` runs each frame
  (`rtxvulkan/scene/scenebuffers.cpp:341`). It matches lamps by index into a list sorted by position
  (`scenedesc.cpp:284`), so a lamp that appears or goes (a glow effect, a bolt) rebuilds the full grid.
  `build` fits the extent exactly to the lamps' reach, so a carried torch or a bolt that moves outward
  fails `covers` and rebuilds the grid on each frame. A lamp that crosses a cell re-`fill`s the full
  `RunList`. Target shape: pad the extent, give lamps stable identities, and update only the bins of the
  lamps that changed. (medium)
- [ ] **Blocked: Q2 in `review-upstream-diff_QUESTIONS.md`.** `components/rtxvulkan/device/memory/slottable.hpp:99-100`, `growablebuffer.cpp:19` — when the rows
  outgrow a copy, `SlotTable::sync` doubles it, makes a new host-written buffer, and rewrites every row.
  The world's top-level row table (`scene/sceneacceleration.hpp:208`) gets this spike on the frame a
  cell pushes it past the threshold. Without resizable BAR, the old and new copies are both in the
  ~246 MiB host-written heap until the graveyard collects the old one. Target shape: fixed-size blocks
  with an address table, as `BlockedBuffer` has, or a capacity set at load. (medium)

## Wrong behaviour in a single place

- [ ] **Blocked: Q3 in `review-upstream-diff_QUESTIONS.md`; the comments are corrected and the lag measured.** `components/rtxvulkan/shaders/lib/historyclamp.glsl:63-66`, `trace/denoise/accumulateclamp.comp:20` —
  both say that the glossy and pane filters keep their means with `heldToFast`. Only
  `accumulateclamp.comp` includes the library. `trace/denoise/specular.comp:126-151` and
  `trace/denoise/pane.comp:77-92` blend with `blendedMean`, with no fast mean and no box. Thus a rough
  reflection, or a pane's light, keeps up to `ACCUMULATE_FRAMES` of old light after a lamp changes on a
  still surface. Commit 2e237e8c8a said that these filters "will" use the clamp. **Decided 2026-10-07: measure
  first.** Target shape: the comments say what the filters do now. Then `./omw release noise --cut=N`
  measures the lag on a lamp change, and the measurement decides whether each filter gets fast means
  and `heldToFast`, as ReLAX clamps specular. (medium)

## One truth has more than one source

- [ ] **Blocked: Q4 in `review-upstream-diff_QUESTIONS.md`.** `components/rtx/renderer/renderer.hpp:438-439,492` — `traceGuiTexture` and `renderFrame` take the
  1616-byte `Shaders::VisibilityConstants` as "the camera". Four parties write its fields: the camera
  builder (`frame/camera.cpp:47-86`), `describeWorld` (`environment/frameworld.cpp:108-214`, which also
  splits the rest into `FrameOptions`), `sampleFrame`, and the backend (`visibilitypass.cpp:484-527`,
  `tracemedia.cpp:58`). `leavesSamplingAlone` (`frame/framesampling.cpp:29-39,46`) exists only to catch a
  writer of another party's field. Target shape: the seam takes a host-side description (eyes, world
  reading, options), and only the backend fills the device block. (medium)
- [ ] `apps/rtxtool/main.cpp:853-854`, `apps/rtxtool/model/runrecord.cpp:19-21` — the rule "hashed =
  `--hashes`, `--against` or `--pictures`" is calculated two times. After `main` sets `mActions.mHash` on
  each stop, the three request clauses in `RunRecord::begin` are redundant. Target shape: keep only the
  per-stop flag. (low)
- [ ] `tools/omw/repeat.py:47`, `tools/omw/perf.py:75` — both pass `--validation=off`, which `bench`'s
  `VerbPolicy` (`validationForMeasuring`) already applies. `repeat` also refuses `--validation=sync`
  because of it. Target shape: remove both. (low)
- [ ] `components/rtx/frame/bluenoise.hpp:17`, `frame/specularalbedo.hpp:26`,
  `environment/fogbuilder.hpp:26` — blue noise and the specular albedo table are process-lifetime
  `shared()` singletons. The fog field is a value that `bakeFogNoise()` makes again for each
  `VulkanRenderer` (`vulkanrenderer.cpp:87`). Target shape: one convention for all three. (low)

## Sibling APIs disagree

- [ ] `components/rtxvulkan/trace/stresspass.hpp:51` — zone ownership is split. `StressPass::record`
  takes a `GpuTimer&` in the middle of its arguments and opens its own zone. `SpriteBinPass`,
  `RipplePass`, `DigestPass` and `VisibilityPass` take `GpuTimer*` last and open their own zones.
  `WavePass`, `Upscaler`, `BloomPass`, `ExposurePass` and `SunGlarePass` are opened by their callers
  (`trace/tracechain.cpp:100`, `vulkanrenderer.cpp:617`, `display/displaychain.cpp:122,159`). Target
  shape: every pass takes `GpuTimer*` last and opens its own zone. (low)
- [ ] `components/rtxvulkan/vulkanrenderer.cpp:112-120,384-385,444-447,715-721` — four hand-written ways to
  empty the queue: `drain`, `finishGuiTraces`, the GUI-texture `finish` before a presenter rebuild, and the
  destructor's. Target shape: one drain on the renderer. (low)
- [ ] `components/rtxvulkan/framering.hpp:164,168` — the names are crossed with the seam's:
  `Renderer::finishFrame` calls `FrameRing::collect`, and `Renderer::collectFrame` calls
  `FrameRing::collectFinished`. Target shape: name the ring's methods after the seam calls. (low)
- [ ] `components/rtxvulkan/texture/texture.hpp:303` — `TextureArray::mPasses` holds a reference to the
  renderer's `TexturePasses` only to give it to `mArrival.record` (`texture.cpp:553`), but
  `bakeComposites` takes its `GroundCompositePass` at the call. Target shape: `write(batch, passes, …)`
  takes `const TexturePasses&`, and the member goes. (low)
- [ ] `components/rtxvulkan/device/memory/buffer.hpp:155,215`, `device/commands.hpp:55,139` — overloads
  with one name do different things. `Buffer::clear()` sets host memory, and `clear(commands)` records a
  device fill. The private `CommandPool::begin()` opens a batch buffer outside the recording bookkeeping,
  and the public `begin(VkCommandBuffer)` registers a frame recording. Target shape: different names (for
  example `zeroOnHost`, `beginBatch`). (low)
- [ ] `apps/openmw/mwworld/worldimp.cpp:554` — a clock jump (`noteHourWritten`) calls
  `RenderingManager::notifyTeleport`, whose doc (`renderingmanager.hpp:211-217`) speaks only of
  `ActionTeleport`. The seam calls this `notifyCut`. Target shape: rename it `RenderingManager::notifyCut`
  and document both callers. (low)
- [ ] `components/rtx/environment/moonbuilder.hpp:159-160` — `addMoonFaces` takes `(…, holds, thread)`,
  but `addCloudSheet`, `addSkyContent` and `readNightSky` take `(…, thread, holds)`
  (`skybuilder.hpp:102-113`, `nightsky.hpp:334-336`). (low)
- [ ] `components/rtx/environment/nightsky.hpp:340` — `readNightSky` takes `const osg::Node&` and casts the
  const away inside (`nightsky.cpp:273`). `readAtmosphere` and `readCloudShell` take `osg::Node&`
  (`atmosphere.hpp:93`, `cloudshell.hpp:293`), so their callers cast it away (`atmosphere.cpp:234`,
  `cloudshell.cpp:250`). Target shape: one constness, with the cast in one place. (low)
- [ ] `components/rtx/image/imagedescription.hpp:53-59` — the two `describeImage` overloads have different
  argument orders. Target shape: one order, with the out-parameters last. (low)
- [ ] `components/rtx/mirror/cells/cellring.cpp:314-323`, `cellplacer.cpp:477` — `CellRing::adopt` fills a
  cell's model list, and then the placer is called two times (`hold`, `adoptPlacements`). Grass does the
  same bookkeeping in one call (`holdGrass`). `dropUnless`/`dropGrassUnless` (`cellplacer.hpp:290-338`) are
  almost copies. Target shape: one adopt call for each kind, and one drop template. (low)
- [ ] `apps/rtxtool/hosted.cpp:85,200`, `apps/rtxtool/main.cpp:924` — the `printLeft` argument always
  equals `request.mPlayed`. Seven call sites (`main.cpp:551,833,882,924,972,1210,1282`) give `variables,
  config, resources, window` again, although `runStops(Command, Framed, …)` exists. Target shape:
  `runHosted(const Command&, const Framed&, SessionRequest)`. (low)
- [ ] `components/rtxvulkan/shaders/trace/denoise/accumulate.comp:103-107` — the push block is written
  field by field (`HistoryConstants frame; uint dualMotion;`), but the host pushes
  `Shaders::AccumulateConstants` (`shared/accumulate.h:176-180`). All other passes declare their header's
  struct. Target shape: `AccumulateConstants constants;`. (low)

## Owners that are not pinned or encapsulated as the conventions say

- [ ] `apps/openmw/mwrender/framedescriber.hpp:66,126` — `FrameDescriber::mFrame` (filled at
  `framedescriber.cpp:96`) holds references into the describer's own `mWorld`, `mEye` and `mJumped`, but
  the class can be copied. Target shape: delete copy and move. (low)
- [ ] `components/rtxvulkan/device/memory/accelerationstructure.hpp:66`, `structurestorage.hpp:35` — each
  structure keeps a raw `StructureStorage*`, and `StructureStorage` can be copied and moved. Target shape:
  delete copy and move on `StructureStorage`. (low)
- [ ] `components/rtx/scene/materialtable.hpp:39,45`, `meshtable.hpp:46,50`, `deformertable.hpp:123-139` —
  the cross-table writers (`MaterialTable::add`/`set`, `MeshTable::add`/`notePosed`,
  `DeformerTable::addRig`/`addMorph`/`stand`/`pose`/`release`) are public, while `hold`/`drop` are private
  behind `friend class SceneDesc`. Through the mutable `SceneDesc::materials()`, a caller can `set` a
  material and skip `PlacementTable::rewriteWearing`. No such caller exists now. Target shape: private,
  with `friend SceneDesc`. (low)
- [ ] `components/rtx/view/offscreentrace.cpp:202-234` — `pick() const` advances the shared traversal
  counter, changes `mPoseStamp`, and runs a cull, through `unique_ptr` members. Target shape: make `pick`
  non-const. (low)

## The driver and its dependencies

- [ ] `tools/omw/deps.py:92-99` (`appimage_tools`), `:102-115` (`crash_tool`) — the AppImage tools and
  Breakpad's two tools are kept under names with no version (`deps/appimage/linuxdeploy`,
  `deps/crash/dump_syms`) and are used again when the file exists. A change in `pins.py` thus never
  reaches a computer that has the tool, nor Windows CI, whose cache restores the old files
  (`.github/actions/openmw-deps/action.yml:51-53`). This breaks the promise at `deps.py:1-3`. Target
  shape: a folder named after the pin's version or digest, as `vulkan_sdk_dir` and `windows_set` have. (high)
- [ ] `tools/omw/fetch.py:114-137` (`extract_member`) — writes directly to the final path. An interrupted
  extract leaves a truncated `dump_syms` or `minidump-stackwalk` that `crash_tool` accepts, because it
  checks only `is_file()`. Target shape: extract into a `.partial` file and rename it (`settle`/
  `build_beside`). (medium)
- [ ] `tools/omw/deps.py`, `.github/actions/openmw-deps/action.yml:48-53` — nothing removes the folder of a
  replaced `VCPKG_TAG`, Qt or SDK version, so the Windows cache grows with each change. Target shape: after
  a fetch, delete sibling folders that the current pins do not name. (medium)
- [ ] `tools/omw/repeat.py:97-100` — a differing run counts as "not repeatable" only with the exit status
  `DIFFERED_STATUS` and a line that starts with `"against "`. `sDifferedStatus` exists so that `repeat`
  does not depend on the report's words (`apps/rtxtool/model/benchrun.hpp:471-476`). Target shape: decide
  on the status alone. (medium)
- [ ] `tools/omw/perf.py:121-124` — gets the frame count and wall time with a regex over the bench report
  text, although `bench --json` writes the same figures. Target shape: `profile` passes `--json` and reads
  it. (low)
- [ ] `tools/omw/noise.py:48-51` — gets the A/B table with a regex over `judgeNoise`'s sentence
  (`apps/rtxtool/compare.cpp:315`). Target shape: `noise` writes a machine-readable record for each side,
  and `noise.py` reads it. (low)
- [ ] `apps/rtxtool/options.cpp:524,540,548,554`, `apps/rtxtool/main.cpp:826-828,876-878` — `--against`
  is a directory under `shot` and a file under `bench`. `shot` writes `hashes.csv` into `--out`, while
  `bench` needs `--hashes=<file>` and `--pictures=<dir>`, and ignores `--out`. Target shape: `bench` takes
  `--out=<dir>`, and `--against=<dir>` means the same for both verbs. `repeat.py`'s `RUN_SWITCHES` becomes
  smaller. (medium)
- [ ] `tools/omw/listing.py:59` — `check` returns success and prints nothing when the build has no
  `components_qt` (`asan`, `tsan`, `release`, and `debug` on Windows). Target shape: say that the check was
  skipped and why, or exclude the Qt-only libraries by rule. (low)
- [ ] `tools/omw/testing.py:19-22`, `omw/main.py` `_build` — `--without-device` removes only the device
  label from CTest. The sanitizer jobs thus compile `rtx-gpu-tests` under ASan and TSan and never run it.
  Target shape: do not build targets whose tests all have the `device` label. (low)

## The upstream merge workflow trusts the dispatched ref

- [ ] `.github/workflows/upstream.yml:94,171` — the checkouts have no `ref: master`. `:113` counts
  `HEAD..upstream/master`, and `:218` makes the merge branch from `HEAD`, which is the dispatched ref. The
  prompt tells the agent that the branch was "made off master" (`:372`), `publish` opens the PR against
  `master` (`:635`), and the bundle excludes only `^origin/master` (`:527`). A dispatch from a feature
  branch thus puts that branch's commits into a PR that merges automatically. Target shape: `ref: master`
  on both checkouts, or refuse other refs. (medium)

## Untrusted input and external packages

- [ ] `components/rtxvulkan/spirv/spirvbindings.cpp:136,140,170` — `readBindings` reads `operands[0..2]`
  of `OpDecorate`, `OpType*` and `OpVariable` without a check of the instruction's length, but its
  contract is to throw `InputError` for an invalid module. A short last instruction reads past the span.
  Target shape: check the operand count for each opcode, as `operandOf` does. (low)
- [ ] `components/rtxvulkan/CMakeLists.txt:460,485` — volk and VMA use `FIND_PACKAGE_ARGS CONFIG QUIET`
  without a version. The code needs volk's Vulkan 1.4 entry points (`vkCmdPushDescriptorSet` in
  `pipeline/dispatch.cpp`), so an old distro volk is accepted and fails to compile. Target shape: put the
  pinned versions in `FIND_PACKAGE_ARGS`. (low)

## Shader structure

- [ ] `components/rtxvulkan/shaders/lib/geometry.glsl:80-81` — `smoothLift`'s `fromCorner[3]` and
  `weight[3]` are indexed by the loop counter. The release `visibilityhit.rchit.spv` has six
  Function-storage array pairs for them. `lib/lights.glsl:469-478` and `trace/denoise/atrous.comp:124-129`
  say such arrays become scratch on this hardware and remove them. Target shape: three named corners and
  selects, or a measurement noted where the arrays stay. (low)
- [ ] `components/rtxvulkan/shaders/lib/sprites.glsl:467-520` — `PuffLayers::mLayers[5]`/`mAt[5]`, walked
  by `addPuff`'s insertion loop, are Function-storage arrays in `visibility.rgen.spv` and
  `spritecomposite.rgen.spv`, live across the full sprite walk. Target shape: as above. (low)
- [ ] `components/rtxvulkan/shaders/trace/visibility.rgen:261-281,337-355` — the arms' peel and the
  world's peel are the same body. They differ only in eye, mask, miss record, arms flag and the arms'
  early exit on a miss. Target shape: one `peelLayers(...)` that returns the last `Answer` and whether it
  ended in a miss, called two times. (low)

## Dead code

- [ ] `components/rtxvulkan/shaders/lib/traversal.glsl:886-905` — `solidBetween` has no caller. Its doc
  speaks of the removed bounce reuse. Target shape: delete it. (low)
- [ ] `components/rtxvulkan/shaders/lib/fog.glsl:186-190` — `fogExtinctionAt` has no caller. Target shape:
  delete it, and point the `fogColumnOver` doc (`:560`) to `fogDensityAt`. (low)
- [ ] `components/rtxvulkan/shaders/lib/surfacematch.glsl:124-133` — `samePlane` is split out for the
  removed bounce reuse. Its only caller is `heldSurfaceMatches` (`:185`). Target shape: put it into the
  caller and remove the reuse words. (low)
- [ ] `components/rtxvulkan/trace/spritebin.hpp:115`, `spritebin.cpp:130` — `SpriteBin::getBytes` has no
  caller, and the bins' tables are not in `SceneStats::mTableBytes` (`scene/devicescene.cpp:197`). Target
  shape: count the bins in the report, or delete the accessor. (low)

## Stale or false comments

- [ ] `components/rtx/renderer/frameimage.cpp:68` — says that the rasterizer's thumbnail is cut by
  `Misc::cropToAspect`. `MWRender::ScreenshotManager` crops in double and does not call it. Target shape:
  the comment says what the rasterizer does. (low)
- [ ] `components/rtxvulkan/shaders/lib/traversal.glsl:366` — the `MEET_BY_CHANCE` doc lists "the reuse's
  rays". (low)
- [ ] `components/rtxvulkan/shaders/lib/payload.glsl:10` — says "twenty-five words". The struct has
  twenty-six (`:144`). (low)
- [ ] `components/rtxvulkan/shaders/lib/bindings.glsl:350-352` — gives cell `c`'s lamps as
  `at[at[c]] .. at[at[c + 1]]`, but `lightRunInCell` (`lib/lights.glsl:95-98`) indexes `2c + key`, with two
  runs for each cell. (low)
- [ ] `components/rtxvulkan/shaders/lib/bindings.glsl:308` — says `IndexList` is "the light grid's, and
  the sprite tiles'". The sprite tiles read `SpriteTileList` (`lib/spritelist.glsl:65`). (low)
- [ ] `components/rtxvulkan/trace/tracemedia.hpp:59` — "Nothing may be in flight: `WavePass::describe`
  says why" contradicts `WavePass::describe` (`wavepass.hpp:35-37`) and `VulkanRenderer::setSea`
  (`vulkanrenderer.hpp:115-117`). Target shape: one statement of the contract. (low)
- [ ] `components/rtxvulkan/trace/tracechain.hpp:39-41` — says pictures "are traced and waited for one at
  a time". Pictures are deferred, and nothing waits for them (`picturetracer.hpp:40-44`). One bin is safe
  only because of queue order (`spritebin.hpp:82-83`). Target shape: give that reason here. (low)
- [ ] `components/myguirtx/rendermanager.cpp:96,199,268` — `checkTexture` says that the backend supports
  external textures, but `doRender` `static_cast`s each `ITexture` to `SlotTexture`, which is undefined for
  an external one. `:268` says "two pipelines", but `GuiPass` has five (`gui/guipass.hpp:225-229`). Target
  shape: correct comments, and an assert that the texture is a `SlotTexture`. (low)
- [ ] `components/rtx/view/offscreentrace.hpp:80` — names `readGuiTexture`, which no longer exists
  (`Renderer::takeGuiCopy`/`takeCopy`). (low)
- [ ] `components/rtx/environment/moonbuilder.cpp:72-74` — `foldedPhase` says "Morrowind's phases are
  multiples of a quarter pi, so the fold is a subtraction". The phase is now the continuous
  `MoonState::mPhaseEighths`. (low)
- [ ] `components/sky/vertexrules.hpp:41-46` — says "exactly white, and nothing else", but
  `starVertexShown` tests only `colour.x() == 1.f`. Target shape: the doc states the red-channel rule. (low)

## Include and header conventions

- [ ] `components/misc/presentation.hpp:1`, `components/sdlutil/sdldisplay.hpp:1` — new fork headers with
  `#ifndef` guards. Target shape: `#pragma once`. (low)
- [ ] `components/sceneutil/stableidentity.hpp:66` — uses `typeid` without `<typeinfo>`. (low)
- [ ] `components/crashcatcher/crashimagelinux.cpp:84,92` — uses `std::size_t` without `<cstddef>`. (low)
- [ ] `components/rtxvulkan/pipeline/shadercode.cpp:97` — uses `std::move` without `<utility>`. (low)
