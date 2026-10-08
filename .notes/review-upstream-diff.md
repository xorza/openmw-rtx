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

## Phase 3 — checks that keep C++, shaders and data in agreement

- [ ] **3.11 A non-finite placement transform reaches the top level.**
  `mirror/sceneextractor.cpp:546` inverts a world matrix that a zero-scale ancestor makes singular.
  `:167` and `:924-976` place the transform unchecked. Sprites and lamps already refuse non-finite
  data (`emitterresolver.cpp:281`, `lightbuilder.cpp:180-187`). Target: in `enterTransform`, fall
  back to `computeLocalToWorldMatrix` when the inverse fails or is not valid. In `addDrawable`,
  refuse a placement with a non-finite element once (`Refused::Mesh`).
  Verify: tests for a billboard under a zero scale and a NaN `MatrixTransform`;
  `./omw test components-tests --gtest_filter='RtxSceneExtractorTest.*'`, `./omw check`.

- [ ] **3.12 A mapped normal of zero length becomes a NaN.** Approved, though it can move
  `HAS_MAPS=1` pictures by a rounding.
  `shaders/lib/traversal.glsl:1176,1268-1269`, `shaders/texture/normalspread.comp:76`. `decodeNormal`
  returns `2·rgb − 1` without a unit length, so (0.5, 0.5, 0.5) decodes to zero. The NaN reaches the
  denoiser's histories, or the companion's mip chain for the texture's life. `traversal.glsl:1083`
  and `:1089` already guard with a select. Target: the same select, falling back to the geometric
  normal, and to (0, 0, 1) in `normalspread`.
  Verify: `./omw kernels --against` (only `HAS_MAPS=1` and `normalspread` move),
  `./omw shot --views=all --map --upscale=off --against=<dir>`, `./omw check`.

- [ ] **3.13 Contracts stated in comments and not asserted.**
  - `device/commands.cpp:116-122`: `CommandPool::collectIdle` does not assert `mOpen.empty()`, which
    `Graveyard::collectIdle`'s comment relies on.
  - `device/timeline.cpp:16`: `waitFor(value)` does not assert `value <= mSubmitted`, so a wrong call
    waits 10 s and blames the device.
  - `device/device.cpp:271-277`: a `vkDeviceWaitIdle` failure other than device loss skips
    `markIdle()`, and `~Graveyard`'s assert then fires in a destructor. Call `markIdle` regardless,
    with a comment.
  - `texture/texture.cpp:672-677`: `TextureArray::getSet` does not assert that no descriptor is owed
    for its slot. After `setAnisotropy`, a trace without a placement would sample a destroyed
    sampler.

  Verify: `./omw test rtx-gpu-tests --gtest_filter='RtxFrameRingTest.*:RtxBatchTest.*:RtxTextureArrayTest.*'`,
  `./omw repeat --pairs=10`.

## Phase 4 — frame-path and load-path costs

Measure each against the baseline bench (median, p99, worst frame), on a quiet desktop, in the
background.

- [ ] **4.1 The mirror moves the deletion of unloaded cells onto the frame thread.**
  `objects.cpp:154,188` push a removed `Animation` to `SceneUtil::UnrefQueue`, and `engine.cpp:265`
  flushes it to a worker before `renderFrame`. The mirror still holds the root (`mFrozen`,
  `sceneextractor.hpp:469-471`), the actors' rig copies, the particle systems and the animated
  placements (`materialresolver.hpp:281`, `emitterresolver.hpp:186`). `thawUnmet` and `retire`
  (`sceneextractor.cpp:761-794,1081-1090`) then drop the last reference inside
  `WorldMirror::mirror`, on the frame of a cell crossing.
  Target: what the mirror lets go of is released where the game releases its own.
  1. The core gets a `Released` sink, a persistent
     `std::vector<osg::ref_ptr<const osg::Referenced>>` cleared after each hand-over, so a steady
     frame allocates nothing. `Kept::retire`/`clear` hand `drop` the key as well as the value, and
     `thaw`/`thawUnmet` append the root.
  2. `SceneExtractor::retire(Released&)` and `detach(Released&)` take it at the call. A caller with
     no queue (the tests, an offscreen view) clears it in place.
  3. `attachWorld` (item 1.11) is also handed the engine's `SceneUtil::UnrefQueue&`. `WorldMirror`
     moves the sink into it after each mirror. The queue takes `ref_ptr<osg::Referenced>`, so the
     one hand-over casts away `const`, with a comment: dropping a reference does not modify the
     object.
  4. `onDetachWorld` hands the last sink over too. The world is destroyed before the engine's queues
     (`engine.cpp:347` against `:359`), so the queue is alive at every detach.

  Depends on 1.11. Measure first: `./omw profile` on a crossing route, looking for `~Node` and
  `Referenced::unref` under `retire`/`thawUnmet`. Then the bench's p99 and worst frame against the
  baseline.
  Verify: a test whose sink holds the only reference after a retire;
  `./omw test components-tests --gtest_filter='RtxSceneExtractorTest.*:RtxFrozenSubtreeTest.*:RtxKeptTest.*'`,
  `./omw test openmw-tests --gtest_filter='RtxWorldMirrorTest.*'`.

- [ ] **4.2 `ChainKeys` erases and allocates again the keys only it holds, and rescans the table.**
  `mirror/chainkeys.cpp:304-310,334-341`, gated at `sceneextractor.cpp:784-787`. Emitters, water and
  refused meshes keep their keys only in `ChainKeys`. On a frame where any material dies, `retire`
  erases them, and the next walk allocates a new `osg::StateSet` for each. Each such frame scans up
  to about 16k pairs at least twice. Target: an epoch on `Held`, stamped in `under()`. `retire`
  erases only pairs that are unheld and unmet this epoch.
  Verify: a test with an emitter and a material that goes on frame 2, and zero allocations on
  frame 3; `./omw test components-tests --gtest_filter='RtxShadingTest.*:RtxSceneExtractorTest.*'`.

- [ ] **4.3 A path string is allocated each time a texture is taken.**
  `mirror/materialresolver.cpp:363` and `emitterresolver.cpp:124,146` build a
  `VFS::Path::Normalized` per material adoption, and twice per emitter arrival. Target: a scratch on
  `ThreadContent` that normalizes in place into a persistent string and returns a `NormalizedView`,
  as `ImageFactCache::of` does.
  Verify: an allocation test for a second material over a standing image;
  `./omw test components-tests --gtest_filter='RtxSceneExtractorTest.*'`.

- [ ] **4.4 `CellHolds::mModels` shifts owning rows on every insert.**
  `mirror/held.hpp:272`, `held.cpp:291-344`. A sorted `flat_set` of rows that each own a vector moves
  every later row, on the frame thread, for each new model. Target: a
  `boost::unordered_flat_map<const PreparedModel*, HeldModel>` reserved at a budget. Only `forget()`
  iterates, and order does not matter there.
  Verify: `RtxCellRingTest.*`, the bench on a moving route.

- [ ] **4.5 Each trace variant reads and parses the same SPIR-V files again.**
  `pipeline/computepipeline.cpp:14`, `tracepipeline.cpp:47`, `shadercode.cpp:91-117`,
  `trace/visibilitypass.cpp:319,332`. 32 variants re-read the 1.5 MB closest-hit module and rebuild
  `readBindings`' map each time. Target: `ShaderCode(device, modules)` reads each module and its
  interface (item 3.1) up front, and `stage()` is `const`. `makeTracePipeline` and `TracePipeline`
  take a `const ShaderCode&`, as `makeGraphicsPipeline` already does. `VisibilityPass` declares its
  `ShaderCode` before the member that owns the launch compile, so the compile joins before the code
  goes. Depends on 3.1.
  Verify: `./omw test rtx-gpu-tests --gtest_filter='RtxShaderCodeTest.*:RtxTracePipelineTest.*'`,
  `./omw kernels --against` (nothing moved).

- [ ] **4.6 The G-buffer's 19 channels overflow the 16-image barrier batch.**
  `device/memory/barriers.hpp:15,71`, `trace/gbuffer.cpp:136-145`, `trace/tracechain.cpp:45`.
  `GBuffer::begin` emits two `vkCmdPipelineBarrier2` a trace, and the comments still say fourteen.
  Target: a `static_assert(CHANNEL_COUNT <= Barriers::sMostImages)` with the room raised, or a
  caller room as `TextureArrival` has. Fix both comments.
  Verify: `./omw test rtx-gpu-tests --gtest_filter='RtxBarriersTest.*:RtxTraceChainTest.*'`,
  `./omw shot --views=all --map --upscale=off --against=<dir>` (nothing moved).

## Phase 5 — design and simplification

### Mirror

- [ ] **5.1 `SceneExtractor` does several jobs, and "changes on its own" has two sources.**
  `mirror/sceneextractor.hpp:322-391,466-485`, `.cpp:340-364,882-938,991-1097`. Whether a root
  changes on its own is `Traversal::mChangeable` for node kinds and `mRecordedChangeable` for
  drawable kinds, combined only in `endFrozen`. `thawUnmet` scans all of `mFrozen` every walk.
  Target: a `FrozenRoots` part that owns the frozen state and is handed the holders at each call.
  `addDrawable` returns whether its placement changes on its own, the traversal ORs it, and
  `mRecordedChangeable` goes. Skip `thawUnmet` when every run was met.
  Verify: `./omw test components-tests --gtest_filter='RtxFrozenSubtreeTest.*:RtxSceneExtractorTest.*:RtxCellRingTest.*'`,
  `./omw shot --against`, `./omw repeat --pairs=10`.

- [ ] **5.2 The resolvers' sweeps are split into halves the extractor must remember.**
  `sceneextractor.cpp:772-794`, `meshresolver.hpp:87`, `materialresolver.hpp:168`. Fold
  `retireDeformers()` and `retireHolds()` into their resolvers' `retire()`.

- [ ] **5.3 The lines-and-points refusal lives in one caller.**
  `mirror/meshresolver.cpp:144-155` refuses with a reason (the null test at `:148` is dead after
  `:81`), and `cells/templatewalk.cpp:149-156` drops the same drawables in silence. Target:
  `MeshReader::read` refuses them, and `drawsLines` moves to `meshreader.cpp`.
  Verify: `./omw test components-tests --gtest_filter='RtxMeshReaderTest.*:RtxTemplateWalkTest.*:RtxSceneExtractorTest.*'`.

- [ ] **5.4 Cell and grass adoption repeat each other.**
  `cellring.cpp:135-145,161-189,304-345`, `cellplacer.cpp:356-375` against `:463-478`. Target:
  `CellRing::adoptModels`, a template for the "handed" test, and `CellPlacer::appendPlacements` used
  by both callers.
  Verify: `./omw test components-tests --gtest_filter='RtxCellRing*'`.

### Core

- [ ] **5.5 Vulkan vocabulary and backend names in the core.**
  `renderer/channel.hpp:40,46,69`, `renderer/framedigest.hpp:20` (`bindingOf`, "binding order").
  `renderer/renderer.hpp:84,89,114,524,529-530`, `scene/mesh.hpp:98`, `scene/scenetextures.cpp:41`,
  `frame/framesampling.cpp:59`, `common/parallel.hpp:29`. Backend classes named in
  `image/textureencoding.hpp:34,37`, `image/texturedata.hpp:442-513`, `scene/compositequeue.hpp:21`,
  `scene/ripple.hpp:8-9`. Target: rename `bindingOf` to `indexOf(Channel)`. The backend says once
  that channel *i* binds at binding *i*. Rephrase the comments in API-neutral terms.
  Verify: `./omw test components-tests --gtest_filter='RtxSourceTreeTest.*'`,
  `./omw test rtx-gpu-tests --gtest_filter='RtxDigestPassTest.*:RtxTraceChainTest.*'`.

- [ ] **5.6 `HeldSlotRows` exists only inside `HeldRows`.**
  `common/slots.hpp:298-373`: two layers of forwarders (8, then 6). Target: one `HeldRows<Row>` with
  public readers and protected `take`/`free`/`hold`/`drop`/`at`, used by the four tables. Port the
  test to a small derived class.
  Verify: `./omw test components-tests --gtest_filter='RtxSlotRowsTest.*:RtxSceneDescTest.*:RtxSceneTableTest.*'`.

- [ ] **5.7 Test-only members in production headers.**
  `TextureTable::getRefused`, `boneAt`/`weightAt`, `encodeShading`/`decodeShading`,
  `MeshTable::getMeshIndices`, the encoding-only `describeImage` overload, and `sAssertsOn` in
  `renderer/renderer.hpp:97-101`. Mark each "Read by the tests and nothing else", as
  `RunAllocator::getHoleCount` is, or move it to `apps/components_tests/rtx/support`. Move
  `sAssertsOn` to `common/`.

- [ ] **5.8 The content cache that holds nothing: keep it, guard it.**
  `preprocess/contentcache.hpp:17-37` (`sHolds = false`), `contentkey.*`, each pass's `digest()`,
  `FinestTexels`' held image (`texture/texturepass.hpp:41-72`), `PassStats::mHits`/`mKeyMs`/
  `mKeyBytes` (always zero, printed by `apps/rtxtool/stopwriter.cpp:365-366`).
  `ContentDigest::addValue` (`contentkey.hpp:44-46`) lacks `HashState::add`'s `!sIsSpan<T>` guard,
  so it hashes a span's address. The scaffold stays, as `architecture.md` §7 documents. Target:
  `ContentDigest::add`/`addValue` forward to `HashState`'s guarded overloads, and the report stops
  printing the zero columns.
  Verify: `./omw test components-tests --gtest_filter='RtxContentPreprocessorTest.*:RtxContentKeyTest.*:RtxContentStatsTest.*'`,
  `./omw scene`.

- [ ] **5.9 `image/` files do several jobs.**
  `image/texturedata.hpp` holds the mip types, the format enum with its traits, and the texture
  data. `image/texels.hpp` mixes format identification with decoding. Target: a
  `textureformat.hpp/.cpp` for the enum, the traits, `readFormat`, `nameOf` and `carriesHeight`.
  Rename `AlphaScratch` to `TexelScratch`, since it serves colours too.

- [ ] **5.10 Two bounds policies for a validated `TextureData`.**
  The alpha readers clamp (`alphaimage.cpp:125,140,187`, `texturepass.cpp:45-48`), and the colour
  readers trust the bytes. `describeImage` already guarantees the bytes. Target: one debug-only
  `levelsFit` assert at entry, and no scattered clamps. Check first whether a test builds a short
  `TextureData` on purpose (`alphaimage.cpp:104-109`).

### Seam and game side

- [ ] **5.11 `Renderer::resolutionChanged` has one caller and costs an upstream hunk.**
  `renderer.hpp:134`, `renderer.cpp:192`, `renderingmanager.cpp:1062-1065`. The same function calls
  `processChangedSettings` at `:1091`. Target: the base `processChangedSettings` re-presents when
  `[Video] resolution x/y` changed. Delete `resolutionChanged` and restore upstream's
  `updateProjection = true;`. Rewrite `RendererTest.thePresentationIsAppliedWhenItMovesAndOnlyThen`.
  Verify: `./omw test openmw-tests --gtest_filter='RendererTest.*'`,
  `./omw test components-tests --gtest_filter='MiscPresentationTest.*'`.

- [ ] **5.12 The seam breaks its own pure-versus-default rule in three places.**
  `renderer.hpp:294` (`eventTraversal`) and `:458` (`applyPresentation`) are pure, and `RtxRenderer`
  answers both with nothing. `renderer.hpp:390`: `setScreenshotWriter` is virtual only so
  `GlRenderer` can build `FrameCapture`. Target: empty default bodies for the first two, and delete
  `RtxRenderer`'s overrides. `setScreenshotWriter` becomes non-virtual, and
  `GlRenderer::saveScreenshot` builds `mScreenshot` on first use.

- [ ] **5.13 Restore `MWBase::Environment`'s frame-rate limit.**
  `mwbase/environment.hpp`, `mwgui/mainmenu.{hpp,cpp}` (`:31,58,107,265`, `mainmenu.hpp:32,64`),
  `windowmanagerimp.cpp`. The removal forces the limit through the `MainMenu` and `MenuVideo`
  constructors, and `MenuVideo` paces its own thread without the renderer. Target: upstream's
  `Environment::get/setFrameRateLimit`, set by `Engine::go` beside `mRenderer->setFrameRateLimit`.
  `MenuVideo` reads it as upstream does. The three upstream hunks go. The loading screen and the
  nested loops stay on the renderer.
  Verify: `./omw test openmw-tests`, the main menu's video under `./omw game`.

- [ ] **5.14 Record `<< std::dec` in `sdlinputwrapper.cpp` as an approved fault.**
  `components/sdlutil/sdlinputwrapper.cpp:274-276`. The user approved the fix. Its comment blames a
  remapped controller, which the port now handles. Target: the comment says why without the stale
  cause (an unhandled event leaves `std::cout` in hex for every later line). AGENTS.md's "Three
  faults the user approved fixing" becomes four and names it.

- [ ] **5.15 The SDL3 port reads display orientation from two displays.**
  `sdlutil/sdlinputwrapper.cpp:209` filters by the window's display, and
  `mwinput/sensormanager.cpp:47` reads `[Video] screen`'s display. Target: the window's display in
  both places. `InputManager` hands `SensorManager` the window, as it does for `KeyboardManager`.

- [ ] **5.16 `TracedView` works out its footprint twice.**
  `mwrender/rtx/tracedview.cpp:94-117`. One private `footprintFromAbove()` that returns the two
  corners (a named struct, not a pair) serves both `coversFromAbove` and `waitsForGround`.

- [ ] **5.17 `SceneFrame::mJumped` is a span into a vector anyone may grow.**
  `framedescriber.cpp:131`, `framedescriber.hpp` (`noteJumped`). A `notifyJumped` between
  `describeFrame` and `renderFrame` would leave the span dangling. Nothing does that today. Target:
  assert in `noteJumped` that no described frame is open.

- [ ] **5.18 A changed vsync queries the surface twice.**
  `vulkanrenderer.cpp:404-406`, `present/swapchain.cpp:185-208`. `rebuildsFor` and `setVerticalSync`
  each enumerate the present modes. `Presenter::setVerticalSync` returns whether it rebuilt. Keep the
  `waitIdle` in `remake`. Settings path only, low priority.

### Harness, `./omw` and CI

- [ ] **5.19 The noise planning lives in `main.cpp`, and its model in `compare.hpp`.**
  `apps/rtxtool/main.cpp:993-1229` (about 240 lines of stop planning), `compare.hpp:121-268`.
  Target: `apps/rtxtool/noise.{hpp,cpp}` with the noise types and a pure `planNoise` that returns a
  named plan struct and throws for the option refusals. `commandNoise` keeps the I/O. Test it with
  no world, as `RtxFilmTest` tests `planFilm`.
  Verify: the new suite, then `./omw release noise --views=<one> --strafe=0 --walk=0 --still` before
  and after (same names, same `noise.json`).

- [ ] **5.20 Three verbs set the radiance width by hand.**
  `main.cpp:818,885,1237`. The set {bench, film, view} is `mMeasures || mPlayed` in `VerbPolicy`.
  Target: `frameFrom` sets `RadianceWidth::Shown` from the policy.
  Verify: `./omw test components-tests --gtest_filter='RtxVerbsTest.*'`.

- [ ] **5.21 Small harness consolidations.**
  `runStops` (`main.cpp:524`) and `runInfo` (`:400`) have one caller each. `commandShot` and
  `commandBench` (`:742-860`) repeat the hash-output setup, so one `hashInto` serves both.
  `crashpadmonitor.cpp:511-519` creates the package folder for a session with no dumps.
  `stopwriter.cpp:366` divides by hand where `Rtx::megabytes` exists.

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
