# The ray-traced renderer: the review's plan, and the prerelease tour

Two pieces of work in one document. Parts 1 and 2 are a review of the renderer and of the diff against
upstream: a refactoring, in batches, with the gate's speed ahead of it, since every batch ends in a gate. Part 3
is a new verb of the harness, `tour`, which flies a content's whole world before a release and reports what it
met.

Scope of the review: `components/{rtx,rtxvulkan,myguirtx,contentstore}`, `apps/openmw/mwrender/rtx`, the seam,
`apps/{rtxtool,rtxbuild}`, their tests, and the diff `97722d48f9..HEAD` in upstream's folders. Read at
`2a2dcd6078`. Nothing was built, run or measured for either part: every cost is derived from the code or quoted
from `docs/research` and `.notes/PERF.md`, the tests were not timed, and the tour's cell counts were read from
the content files. The gate's speed is the exception: Part 1's section on it and Batch 0 were measured, on a
low-end AMD integrated GPU under Windows between `e21fad4c21` and `9e66e68b8b`.

## Branches

- **The gate's speed lands on `master` first.** Batch 0 goes on a short branch of its own, is merged, and
  `rtx-review` is cut after it: the gate's speed changes nothing a picture reads, and every other line of work
  pays the gate too. Refused: riding on `rtx-review`, which leaves `master` the slow gate for seventeen
  batches. Decided 2026-11-13.
- **The refactoring follows, on its own branch.** Batches 1 to 17 go to `rtx-review`, in their order, and
  no tour code goes there. It changes how the renderer is built and must leave every picture and figure where
  it was, but for Batch 2's defects, which name what they move.
- **The tour branches off the refactoring's branch, once the refactoring is done.** Part 3 goes to `rtx-tour`,
  cut from the tip of `rtx-review` and not from `master`, so it stands on the harness the refactoring left:
  the one `ExitStatus`, `Verb` and `VerbSet`, a `plan<Verb>` a verb. No refactoring
  item goes there; one found while the tour is written goes back to `rtx-review`, and the tour's branch takes
  it from there. The tour adds a verb nothing else calls, and is reviewed, merged or dropped by itself.
- **Each branch records its own upstream changes.** The refactoring's are Batch 16's; the tour's one, the
  log's listener chained, is entered in `.notes/UPSTREAM.md` on the tour's branch with the change itself.
- **A batch's measurements belong to its branch.** Batch 1's spikes and every **measure** item are the
  refactoring's; the first tours and their figures are the tour's.

How an item is marked:

- **bug** — a defect read in the code, end to end. Never dropped.
- **measure** — the code path is read, the gain is not known. The named run decides it before the change is taken.
- **propose** — a change to upstream code, which waits for a yes (`AGENTS.md`, Rules).
- unmarked — structural, read in the code.

# Part 1. Proposal

## Root causes

- **A stage keeps its own copy of what another stage already holds.** The reader's float geometry after adoption, an
  `InstanceRecord` per slot beside its `PlacementRow`, a 65×65 grid's indices and texcoords per landed cell, blend
  weights widened to floats in three places, a tangent word for every vertex of meshes with no map, a decoded alpha
  plane beside the texels that carry it, the frame read back into two full-size host buffers, a `mShown` image that
  exists as a blit's source, the pipeline cache's blob copied to be digested, `RunSetup` in four structs. Each stage
  keeps only what a later stage still reads, at the source's width.
- **Work is done blind to who reads it.** Every touched slot is rewritten a frame later whether or not it moved;
  every froxel is traced whether or not a pixel reads it; an ambient ray is cast for a flame that never asks; the
  sea is synthesised inland; the pane channels are stored on every pixel; the terminator term is worked out where
  nothing is mapped; every file is stamped though only small ones are ranked. The tree's own answer is a list of
  where there is work (`StageTiles`, `mHoldsNothing`, `getMoved`); these are the places it was not applied.
- **A guarantee rests on a comment or on what drivers happen to do.** Subgroup reconvergence, a deque's stability
  under a concurrent `push_back`, a push and its announcement in two lock holds, "the reader thread asks static
  records only", "one thread records", a hand-kept list of shader alignments, build flags "the same" at two sites,
  a free list "of tens of entries", source-tree rules that pass when they match nothing. Each becomes a type, an
  assert or a test.
- **A rule outlived its cause.** Position fetch kept for candidates after committed hits left it; comments that
  describe mapped host writes over code that stages them; casts and null guards written for a compiler the tree no longer accepts;
  bands in a test for an order dependence a seed removed. Each rule names its cause where it stands, and is
  re-derived when the cause moves.
- **Two routes to one result.** A triangle's corners, a texture slot with and without a hold, a freed slot pulled
  and pushed, a frame's PNG written three ways, a JSON number, an exit status, a scratch path in a test, a refusal
  asserted seven ways, six setters for one profile, three worker pools, three error payloads.
- **Owners were chosen by construction order.** Vertex streams split between two classes, the refit's state in one
  and its logic in another, per-scene samplers that depend on the device alone, a command pool that is also the
  staging allocator, a device that also writes two reports, frame pacing in the seam's base class, verb policy in
  the game-linked `main.cpp`, checks judged by the picture writer.
- **The reference shares the estimator.** `noise` holds a frame against 256 frames of the same transport, so a bias
  in the transport itself (coned cutouts, one scalar lamp fraction, an unregularized lobe at an indirect vertex)
  is a number nobody has.
- **Bounds are compiled in.** The content store's tables and queue are constants, so its tests pay production sizes
  and its bound-reached paths cannot be reached at all.

## Target design

**Data**

- A placement is declared once: one function makes its chain-derived facts for the walk and for the ring, and
  `Stood` holds that type. The settled set holds slots that had motion. `InstanceRecord` is scratch for changed
  slots, made by one function that inverts once.
- Per-vertex data that few meshes carry stands in runs of its own (tangents, as second texcoords already do);
  topology every landed cell shares is one run; weights stay bytes.
- The content store's entry tells its kind without its payload, and "ranked at the sweep" is a property of kind and
  size that the stamps and the sweep both read. Its bounds are `Policy` fields.
- A chain has one plan, computed from the header and read by `run`, `madeLayout`, `scratchOf` and `fits`; one
  identity by what names it, in the preparation and in play.
- The frame ends where it is shown; a preserved copy is made only for a reader that asks.

**Ownership**

- Worker threads: one hands helper over `Misc::Monitor`, whose `give` is the push; one owner hands each service its
  share of the machine. Every queue states its bound.
- What a reader thread may ask of the game is a type: a static-only view made once, not a comment on a class that
  "holds nothing".
- `OwnedBy` on every single-thread part (`CommandPool`, `Timeline`, `Refusals`, `Released`), or the type removed.
- The shared-ownership inventory, each with its verdict:
  - `ContentStore::Store::Resident::mPayload`, `shared_ptr<const ByteBuffer>` — kept: immutable, replaced under
    readers that copy outside the lock.
  - `shared_ptr<OffscreenView>` (`tracedoverlay`, `pendingpaints`, `mapoverlay.hpp`) — kept: a paint outlives the
    local map's hold.
  - `make_shared<vector<ref_ptr>>` in `WorldMirror::release` — kept: upstream's `UnrefQueue::push` signature; a
    transfer.
  - `std::shared_future` in `Job` — kept: std's own.
  - `osg::ref_ptr` held as address keys (mirror identity, `ChainKeys`, `FrozenRoots`, the resolvers, `MeshReader`,
    `TextureTable`, `cells/prepared`) — kept: an address key must own its object.
  - `osg::ref_ptr` images in transit to workers (`QueuedChain`, `Job`, `ChainPass`, `ConversionPass`) — kept: the
    job outlives the asker's hold.
  - `MadeImage` in `PreparedImages`, `SceneTextures`, `TextureRow` — kept: several scenes wear one image.
  - `osg::ref_ptr` to renderer-made nodes (`TracedTerrain`, `WorldMirror`, `RtxRenderer`, `TracedOverlay`,
    `TracedView`, `OffscreenTrace`, `myguirtx`) — kept: OSG's contract.
  - `osg::observer_ptr` `mFile`/`mPartner` in `PreparedImages` — **replace** with a key by what names the image
    (`ChainIdentity`), which removes the observers and the `isGone` sweep. `mWatched` — kept.
  - `Misc::Monitor` in `CellSupply`, `PreparedImages`, `Store`, `HomeKey`, `CardWatch` — kept; `give` changes
    (Batch 2).
  - `ContentPreparation::mMutex` and two condition variables — kept in shape, moved onto the shared helper.
  - `KeptStamps::mMutex`, `runInParallel`'s exception slot, `Graveyard::mLock`, `PipelineCodeFolder::mLock`,
    `ValidationLog::mMutex`, `VisibilityPass::mCanonicalMutex` — kept.
  - `once_flag` for rgbcx/bc7enc and volk; `thread_local sFiledUnder` — kept: forced from outside, reason stated.
  - `scene/scenedesc.cpp:27` `sIdentities`, a process-global atomic — **replace** with an identity issued by
    whoever hands out the scene (low).
  - Atomics for claims and progress (`parallel`, `job`, `TupleQueue`, `VisibilityPass`), `Timeline::mSubmitted`,
    `MemoryAllocator::mHeld`, the store's log-once flags, `CardWatch::mStartAsked` — kept.
  - `SceneDesc&` in the resolvers, the extractor, `CellPlacer`, `SceneUploader`; `FrozenRoots`' four sibling
    references; `Store*` in six holders; `CommandPool&`/`Uploads&` in per-call structs — kept: one pinned owner,
    or thread-safe by design.

**Module layout**

- Core: the wave model (`wavecascade`, `wavespectrum`) moves from `environment/` to `world/`, and the source-tree
  test holds "the backend includes core folders up to `renderer`". `docs/architecture.md`'s two order tables are
  checked against the test's `sFolderOrders`.
- Backend: `device/memory/staging` and `batch` leave `commands.*`; `SceneGeometry` owns every vertex stream;
  `BottomLevelStore` owns all bottom-level work, the refit included; `ScenePasses` owns what depends on the device
  alone; `FrameTargets` owns the targets and their byte count; `PipelineReport` and `FaultReport` leave `Device`.
- Game side: a `FramePacer` the engine owns; the ray tracer's start and its failure wording in `rtx/`;
  `RendererKeys` the one listener.
- Harness: a `plan<Verb>` per verb in the library; `checks.cpp`; one `PictureWriter`; `Verb` and `VerbSet`;
  one `ExitStatus`.
- Shaders: `fogfield.glsl` beside `froxel.glsl`, `puff.glsl` for what both sprite walks share.

## The gate's speed

What `./omw gate` costs on the weak machine, a low-end AMD integrated GPU under Windows, and how to cut it
without dropping a claim it makes: Batch 0, ahead of the review's batches because each of them ends in a gate.
Every figure is from
[`docs/research/2026-11-13-gate-step-times.md`](../docs/research/2026-11-13-gate-step-times.md), measured on
that one machine between `e21fad4c21` and `9e66e68b8b`; nothing was measured on a high-end NVIDIA laptop GPU.

**No whole gate has run on this machine at `9e66e68b8b`.** At `e21fad4c21` it stopped at `tests`, on two tests
that `d0bb9081b3` has since fixed; the steps after `tests` were timed by themselves.

### Where the time goes

| step | seconds | |
|---|---|---|
| format check, spellings, lint, the driver's tests | 12.8 | |
| build, the listing check | 3.8 | nothing to build |
| the release compile | 362.0 | two commits' worth: 277 objects, 28 links |
| tests, their timing | 59.6 | the two GPU shards bound it at 57 to 58 s |
| the integration tests | 67.2 | |
| check | 178.7 | |
| repeat | 194.1 | |

516 s with nothing to compile, of which `check` and `repeat` are 373. A change to the renderer adds its debug
build and the same objects again in the release compile.

### The idea of a lower resolution

- **It does not apply to the GPU tests.** `rtx-gpu-tests` already draws 16 to 65 pixels a side, mostly 32, 33
  and 64 (`RtxVisibilityTest::shoot`'s `size`). What it pays for is the validation layers' shader checks, below.
- **It applies to `check` and `repeat`, and halves both.** They draw 1920x1080 and are bound by the card:
  119 ms of card zones in a 133 ms frame. At a quarter of the pixels `check` takes 89 to 93 s and `repeat` 85 to
  86 s, with the same 59 checks asked, none failed, and the pair identical.

### Root causes

- **The world runs draw a picture no claim reads.** Fifteen of `check`'s sixteen claims are about what the
  renderer was handed, the camera, the sky, the queue and the frames in flight; `finite` alone counts over pixels,
  and `repeat` hashes them. What a place holds is the same at every size, to the tenth of a MiB.
- **A world stands 420 frames before it is whole, in three processes a gate**, each a frame at full size:
  `check`'s first stop and both of `repeat`'s runs (`MeasureWindow`, one cell a frame by `CellRing::adoptHanded`).
- **The release compile builds the test binaries, which nothing runs in that flavour**: 138 of the sample's 277
  objects and 59% of its compile seconds.
- **Every validated process runs the layers' shader checks over pipelines `check` has them run over anyway.**
  The checks are 4.6 s of a GPU test process's 7.3 s start and 44 of its 46 CPU seconds, and they go on through
  its tests: 103 CPU seconds a shard against 29 without them. The kernels' tuples are specialization constants,
  which the layer's cache of checked modules, keyed by a module's code, saves a third of and no more. `check`
  makes every kernel of the same shader set under the same layers, 338 pipelines to the tests' 337, every gate.
- **That CPU is why the GPU binary does not scale past two processes here**: four and six take as long as two
  with the checks, and 33 and 30 s without them against 51 to 57.
- **Every size the gate draws is whole workgroups.** The shaders' groups are 8 a side, and 1920x1080 and
  1280x720 divide by 8 each way: no world run draws a partial group, and no chain that halves meets an odd
  extent in its first three levels.
- **The gate's order is not its costs' order**: the release compile, minutes on a change, runs ahead of a minute
  of tests.

### Decisions

- **`check` and `repeat` each state a frame of their own, 957x539, where the line names none.** The size belongs to the
  verb, as `check`'s hold already does (`sCheckHoldMs`, "where the line names no `--hold` of its own"), so
  `./omw check` and the gate's are one run on every machine. Refused: a size chosen by the machine, which makes
  what a green gate proved depend on where it ran; a size only the gate passes, which leaves two `check`s to tell
  apart; a ninth of the pixels (640x360), which saves 28 s more of the 373 for under half the pixels again,
  because the host bounds the frame from a quarter down (the ship's frame 56.7 ms against 44.4, the walk alone
  23 ms).
- **What the smaller frame costs is said where the size is stated**: `finite` and `repeat` see a quarter of the
  pixels a frame, so an event one pixel in a million meets is met a quarter as often. No class of fault leaves:
  the layers, the claims about the scene, the frames in flight and the processes' agreement do not read the size.
  A full frame stays one switch away, `--size=1920x1080`.
- **`tests` and `their timing` run ahead of the release compile.** `gate.py` states its order as the order of
  cost, and on a change the compile is the dearer by minutes. A green gate takes as long; a red test says so
  minutes sooner.
- **The release compile builds the programs and no test binary.** A target a test runs leaves `all`
  (`EXCLUDE_FROM_ALL`, set in `openmw_add_test`), and `omw build` still names the tests, as
  `Build.build_default` already does. Refused: every program as now, 59% of the sample's release compile
  seconds and two of its seven longest links for binaries no workflow builds in that flavour (`ci.yml`'s four
  legs are debug, asan and tsan, and `rtx-release.yml` runs `omw archive`). The cost: a test source that warns
  or fails to compile under `-DNDEBUG` alone is found by whoever next runs `./omw release test`. LLVM does the
  same: its unit tests are out of `all` unless `LLVM_BUILD_TESTS` says otherwise, and the `UnitTests` target
  builds them. No test target is installed, which is the one thing CMake warns an excluded target about.
  Decided 2026-11-13.
- **The gate's `repeat` stays at one pair.** Refused: two pairs at the smaller frame, a second comparison for
  43 s a gate. `AGENTS.md` already asks for `--pairs=10` after touching anything a frame reads, which at a
  quarter of the pixels takes less than four pairs do today. Decided 2026-11-13.
- **The frame is odd on purpose: 957x539, traced at 638x359.** No extent of the shown or the traced frame is a
  multiple of 8 and three of the four are odd, so every 8x8 pass draws a partial group and every chain that
  halves meets an odd extent by its second level; a resized window gives a player such a frame. It costs what
  the round one does: `check` 90.7 and 90.7 s against 92.7 and 91.4 s at 960x540, `repeat` 85.7 against
  84.9 s, every check passing and both pairs identical. Khronos's OpenGL ES conformance run draws at 64x64 and
  then again at 113x47, Dawn's mip-chain tests at 31x4, and this tree's GPU tests at 17, 33, 65 and 129.
  Refused: 960x540, the size `AGENTS.md` names for a bench on a weak machine, of whose four extents only 540
  is not a multiple of 8, so the traced frame stays whole groups as it is today. The cost: a size nobody
  recognises in `--help` and on `check`'s pictures, and a fault it finds first shows as a red gate. Decided
  2026-11-13.
- **The layers' shader checks run in `check` alone.** The GPU test binary's devices and renderers ask for
  every check but those (the layer's `check_shaders`, the spec's `VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT`),
  which compare each pipeline with its shaders and run `spirv-val` over each specialized module; `check` keeps
  them, over every kernel the renderer makes, and the build already runs `spirv-val` over every module. The
  layer's own settings file calls them "CPU intensive during application start up", its presets take one area
  of validation at a time, and Dawn's tests default to a `partial` backend validation with the costly parts
  left to `full`. Refused: the checks kept on the tests' raw devices as well, no loss of coverage for a gain
  nothing measured; everywhere as now, 4.6 s a start and 74 CPU seconds a shard. The cost: a pipeline only a
  test makes, from a test's own shader or tuple, is no longer compared with its shader by the layers, and
  `./omw test` by itself no longer runs the shader checks over the kernels; the gate does. All 499 GPU tests
  pass without them. Decided 2026-11-13.
- **The GPU binary runs in four shards once its processes stop checking shaders.** Measured with the step's
  other suites beside them: 35.4 s against 55.6 s as now, and 31.8 s in six. Four, because six buys 3.6 s for two
  more starts and devices, and `cmake/Tests.cmake` found a third process no gain on another card: the count is
  measured there before it lands (Batch 0). Sharded by `GTEST_TOTAL_SHARDS`, which runs each test exactly once
  across the shards, and not a test a process, since every process stands up two devices and two renderers.

### Refused

- **Keeping a step's result while its inputs stand** (Bazel's test cache, or a stamp file Ninja tracks). The
  four costly steps read what no build graph holds: the driver, the layers, the player's `openmw.cfg` and
  content. Bazel reruns exactly such a test whatever the cache says (`--cache_test_results`, a test tagged
  `external`), and a stale green is the worst answer a gate can give. The steps a cache could soundly keep sum to
  thirteen seconds.
- **The shader checks off in `check` as well.** Then nothing a gate runs compares a pipeline with its shader.
- **A suite of the test binary that runs the shader checks once.** It computes what `check` computes, for 46 CPU
  seconds beside the shards.
- **The layer's cache of checked shaders made to cover the tuples.** It is keyed by a module's code and the
  tuples are specializations of one module; that is the layer's design, not a setting.
- **The release compile beside the device steps.** The one lever that would hide the compile whole, and this is
  the machine it cannot be pulled on: a gate here was stopped for memory inside its release compile
  (`.notes/PERF.md`), and `crash.matrix` has already failed once under a loaded gate (`.notes/ISSUES.md`).
- **Two world runs side by side.** `AGENTS.md` shares the card between no two runs, and `check`'s `queue-held`
  and `frames-overlap` time the queue. Reopened only by a measurement of twenty clean gates run that way.
- **CTest entries by suite in place of shards by index**, this review's first reading of `cmake/Tests.cmake`.
  A suite is no unit to balance by, `RtxVisibilityTest` being 208 of the 499 tests, and the fixture it would
  save every shard, the unvalidated renderer, takes 0.14 s.
- **The prelude beside the build.** Thirteen seconds, and the gate would no longer stop at its cheapest failure.
- **`check` on the release build.** Faster by the asserts it would lose.
- **A shorter crossing, a shorter warm-up, fewer stops.** Each is a claim asked of less: `rooms-held` and
  `crossings-append` are about a long walk.
- **The integration tests in parallel or at another size.** Upstream's script, at 640x480 already, and its tests
  wait on the wall.
- **Frames ahead of a whole world closed untraced.** Under the smaller frame the host's walk bounds them, so
  the trace is not what they cost, and a history a measured frame reads would start elsewhere.

### What Batch 0 comes to

- **With nothing to compile, 516 s becomes some 300 s** on this machine: `check` 178.7 to 91, `repeat` 194.1 to
  85, and the test step's 59.6 to some 37. The last is the step's processes timed side by side, not a `ctest`
  run.
- **The release compile loses its test objects**: 59% of the sample's compile seconds. Its wall without them
  is not measured.
- **What is left is the host in a build with every assert**: 1537 frames of `check` at a median 53 ms and
  1800 of `repeat` at some 42, the walk 23 ms of a frame at the ship. That is the renderer's own cost and no
  part of this plan.

### Sources

- Khronos, Vulkan-ValidationLayers, `layers/vk_layer_settings.txt`: `check_shaders` and `check_shaders_caching`,
  <https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/layers/vk_layer_settings.txt>
- Khronos, Vulkan-ValidationLayers, `docs/shader_validation.md`: when shaders are checked, what the cache keys
  on, and that specialization constants are injected for the checks,
  <https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/shader_validation.md>
- Khronos, VK-GL-CTS, `glcKhronosMustpassEs.hpp`: the conformance run's surfaces, 64x64 and 113x47,
  <https://github.com/KhronosGroup/VK-GL-CTS/blob/main/external/openglcts/modules/runner/glcKhronosMustpassEs.hpp>
- CMake, the `EXCLUDE_FROM_ALL` target property,
  <https://cmake.org/cmake/help/latest/prop_tgt/EXCLUDE_FROM_ALL.html>
- LLVM, "Building LLVM with CMake": `LLVM_BUILD_TESTS` and the `UnitTests` target,
  <https://llvm.org/docs/CMake.html>
- GoogleTest, "Distributing Test Functions to Multiple Machines",
  <https://google.github.io/googletest/advanced.html>
- Bazel, the user manual, `--cache_test_results`, <https://bazel.build/docs/user-manual>

# Part 2. Plan

Whoever addresses an item deletes it. Each batch leaves the build and the tests green.

## Order

- **What every later batch pays for goes first.** Each batch ends in a gate, so the gate's speed is Batch 0,
  and inside it the items stand by what each saves the gates after it.
- **A decision ahead of the batch it decides.** Batch 1's spikes feed Batches 5, 7 and 12, and stand in that
  order.
- **Defects ahead of structure.** Batch 2 holds every **bug**, whichever batch its files belong to: a defect
  does not wait behind a refactoring, its fix is read alone, and the pictures the fixes move are moved once.
- **The guards ahead of what they guard.** Batch 3 is the tests' own rules and support: a source-tree rule that
  passes when it matches nothing holds nothing while files move, and a test double over `NullRenderer` meets
  Batch 7's change of the interface once.
- **Then bottom up, in the tree's own order**, so a batch builds on the final shape of what it includes: the
  threads (4), the store and the preparation they serve (5), the mirror (6) and the scene's tables (7) in the
  core; the device layer (8) under the backend's scene (9), its shaders (10 to 12) and its frame (13); the game
  side (14) over both; the harness (15) over the game.
- **The record last.** The diff against upstream (16) follows every batch that proposes a change to it, and
  the conventions a tool holds (17) are fixed in what survived.
- **Inside a batch**: what a reading settles, since it may be a defect; then structure; then the **measure**
  items, so an optimisation is written once, against the owner the batch leaves. An item that leans on another
  stands under it and says so.

## Batch 0 — The gate's speed

Files: `tools/omw/{gate,repeat}.py`, `cmake/Tests.cmake`,
`apps/rtxtool/{toolline.hpp,options.cpp,main.cpp,framed.hpp}`, `components/rtx/renderer/renderer.hpp`,
`components/rtxvulkan/device/instance.cpp`,
`apps/components_tests/rtxvulkan/support/device/harness.cpp`, their tests.

First, because every batch after it ends in a gate; inside it, in the order of what each item saves the gates
that follow. No item touches upstream's files, and each figure
goes to `docs/research/2026-11-13-gate-step-times.md`.

- [ ] One `./omw gate` at the tip, timed whole: none has run on the weak machine since `e21fad4c21`, where it
  stopped at `tests`, and the batch starts from a gate that passes and a time for all of it.
- [ ] `cmake/Tests.cmake` (`openmw_add_test`) — the release compile builds `all`, and `all` holds the nine
  targets the tests run: 59% of a sample's release compile seconds. The function sets `EXCLUDE_FROM_ALL` on its
  target, with the reason beside it; `Build.build_default` names the test targets already, so `omw build`,
  `omw test` and CI build what they built, and `_release_compile`'s comment says what it no longer compiles.
  Held by: `ninja -t targets all` in `out/release` names no test binary and still names every program;
  `./omw build` in a fresh debug folder builds all nine; `./omw release test components-tests` builds and runs;
  CI's `cmake --install`, which brings `all` up to date, installs what it did.
- [ ] `tools/omw/gate.py` (`STEPS`) — `tests` and `their timing` move ahead of `the release compile`. The help's
  line follows from `SUMMARY`; `test_main.py` holds the two to each other already.
- [ ] **measure** the release compile's wall for one renderer header touched, before and after the two items
  above.
- [ ] `apps/rtxtool/{toolline.hpp,options.cpp,main.cpp,framed.hpp}` — `check` draws its own frame, 957x539,
  where the line names no `--size`. `ToolLine::Frame::mSize` becomes a
  `Named<FrameSize>` as `mHold` is, and `commandCheck` takes the verb's frame where it is not named, beside the
  hold. One constant beside `sCheckHoldMs`, whose comment carries the decision, its cost, and which groups and
  chains the size is odd for; `--help`'s line for `--size` says what `check` draws.
- [ ] `tools/omw/repeat.py` — `repeat` walks at the same size where the line names none: a default
  beside `WALK` and `TAKEN`, not one of `TAKEN`, which the line may not name, so a difference is still read at
  any size. The two constants are one fact in two programs: a test of the driver reads the harness's and holds
  `repeat`'s to it, as `test_main.py` holds `HARNESS_VERBS` to `verbs.cpp`.
- [ ] Tests of the two: `apps/components_tests/rtxtool/options.cpp`, `check` with no size names none and
  `--size=1920x1080` is that, and `bench` with none is still 1920x1080; `tools/omw/tests/test_repeat.py`, the
  line `repeat` builds carries the size once and a named one replaces it. What `commandCheck` makes of an
  unnamed size is out of a test's reach until Batch 15 moves the plans into the library.
- [ ] A known hit at the new size. `repeat` still says NOT repeatable for a fault it was written to catch: with
  `4924c912f8`'s nearest-cell adoption put back to the first cell handed, in a scratch worktree, `./omw repeat
  --pairs=3` differs. And `check` still fails: one barrier of the cascade taken out, the layers report it.
  Neither change is kept. Then `./omw repeat --pairs=10` once.
- [ ] `components/rtx/renderer/renderer.hpp` (`ValidationOptions`),
  `components/rtxvulkan/device/instance.cpp`, the GPU tests' `support/device/harness.cpp` — whether the layers
  check shaders is said by `ValidationOptions`: an `enum class` and not a flag, checked unless said otherwise,
  beside `ValidationLevel` in the core and in its words, naming no layer and no API. `Instance` alone turns it
  into `VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT`, where today it disables nothing. The one place that asks for
  none is the GPU tests' support (`makeHarness` and `describeRenderer`), with the reason and where the checks
  run instead.
- [ ] `apps/components_tests/rtxtool/options.cpp` — a test holds `check` to the shader checks and to
  the layers, since the tests now lean on it. And a known hit: in a scratch worktree a kernel's layout loses a
  binding its shader reads, and `./omw check` reports the layers' error and fails. Not kept.
- [ ] `cmake/Tests.cmake:56` — the GPU binary runs in four shards, split by test index as now, once its processes
  stop checking shaders; the comment there is rewritten from the measurements: what a process pays to start,
  why two were the most with the shader checks on, the figures for two, four and six. The review first read the
  split by index as waste, the DLSS renderer, the kernels device and the unvalidated harness built in every
  shard, and asked for entries by suite. Measured, the unvalidated renderer's 337 pipelines take 0.14 s, and a
  suite is no unit to balance by: `RtxVisibilityTest` is 208 of the 499 tests and over half their time.
  **measure** what the DLSS renderer and the kernels device cost a shard before either moves. The timing suites
  (`stresspass.cpp:92-105`, `presenter.cpp:209`, `gputimer.cpp:169`) take a `RESOURCE_LOCK` only if one fails
  beside its neighbours: none did in a dozen runs of two to eight processes.
- [ ] **measure** `./omw test` on the weak machine after the two items above, and `components-tests`' own time
  inside it: 37 s in one process beside two checked shards, 19.7 s alone. If it bounds the step it is registered
  in shards as the GPU binary is; four took 7 to 12 s beside four GPU shards.
- [ ] **measure** on a high-end NVIDIA laptop GPU, in one sitting: `check` and `repeat` at 1920x1080 and at the
  new size, and two shards against four with the shader checks off. The size and the count are each one for
  every machine: where the host already bounds the frame at 1920x1080 the note says the size bought that machine
  nothing and cost it nothing, and the count is four unless it is slower there than two.
- [ ] `AGENTS.md`, Verification — `repeat`'s line says the frame it walks at, and `./omw test`'s that the
  layers' shader checks are `check`'s. Proposed as wording.
- [ ] The decisions of Part 1's section on the gate fold into their owners, and the section goes: the frame and
  its cost into `framed.hpp` and `repeat.py`, the order and the release compile's reach into `gate.py`, the
  excluded targets and the shard count into `cmake/Tests.cmake`, where the shader checks run into
  `renderer.hpp` and the tests' `harness.cpp`.

## Batch 1 — The open decisions, each settled by its run

No code lands here but a spike behind a switch. Each item ends in a decision written into the batch it names,
and its figures in a dated note under `docs/research`. Run on a quiet desktop, one run at a time.

- [ ] **The content store's layout** (`contentstore/store.cpp:519-537`) — the start sweep reads whole, and
  digests, every file of 4 KiB or less to learn its kind, then drops the kinds held at any size, which the
  preparation reads again. Two shapes: the kind in the path (`v<n>/<kind>/<xx>/<key>`), so an unranked kind is
  not listed at all, which changes the format and empties every player's store once; or the 56-byte trailer
  read first, no format change, the opens kept. Time the store thread's start under the large profile, warm and
  with a cold page cache, with the count of files opened and bytes read by kind. The path only if the opens
  themselves are what costs; the trailer if the reads are; neither if the start is under what a loading screen
  hides. The choice goes to Batch 5.
- [ ] **The host geometry mirror** (`rtx/scene/meshtable.hpp`) — `MeshTable` holds every vertex at 36 B and
  every index at 4 B for as long as its mesh stands, and after arrival only a rebuild (`setScene` after a
  resize or a mode change lets the world go) and the harness's digest read it. Freeing it trades RAM for a
  read at the next rebuild, against the rule that what was read once stays in memory. Read
  `getGeometryBytes()` at the three contents from a scene report, and count the rebuilds of a played session.
  It is freed only if the bytes are large and the world can outlive a target rebuild, so nothing reads the
  mirror again; otherwise it stands and its header says why. The choice goes to Batch 7.
- [ ] **Lamp halos in fog** (`shaders/lib/fog.glsl:587`) — lamps scatter isotropically (`INV_FOUR_PI`) where the
  sun and the moons take the fitted Mie phase, so a lamp in fog has no halo toward the eye; Frostbite evaluates
  the phase per light (https://www.slideshare.net/DICEStudio/physically-based-and-unified-volumetric-rendering-in-frostbite).
  Spike a phase-weighted sum beside the phase-free one the puffs read, behind a switch. `./omw shot --map
  --upscale=off --out=<dir>` without it and `--against=<dir>` with it, at a lamp seen through fog indoors and
  one outdoors at night, then `./omw release noise --ab=<switch>` there and the `air` zone, since a peaked
  phase is one more channel and may be noisier. **Decided 2026-11-13: lamps take the medium's phase**, as the
  sun and the moons do: one medium has one phase function, which Frostbite evaluates per light, Unreal's fog
  holds as one fog-wide `Scattering Distribution` with an intensity a light
  (https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine), and Assassin's
  Creed 4 took as Henyey-Greenstein for its local lights. The spike measures the noise and the `air` zone
  alone; a noisier frame is answered by its sampling or its filter and not by the isotropic sum. Lands in
  Batch 12.
- [ ] **Precipitation in the sprite self-shadow** (`shaders/scene/spriteshade.comp:69-72`) — `spriteShadeWants`
  admits falling snow to a self-shadow whose stated purpose is a smoke column's lit and shaded sides, and the
  file's own figure under snow is 0.64 ms. Exclude `EMITTER_FALLS` behind a switch. `shot --against` at a
  snow view and a rain view, and the `shade` zone's median and p99 under snow from `./omw release bench`. A
  picture: if snow reads the same without it, it goes; the choice goes to Batch 12.

## Batch 2 — The defects

Files: named at each item.

Every **bug** of the review, ahead of the batch its files stand in: the races first, then what no specification
promises, then what is drawn wrong, then what is latent or cannot fail. Pictures move here, by the winding, the
encoder and the clamp, and in no structural item after.

- [ ] **bug** `prepare/contentpreparation.cpp:395` — `makeChain` indexes `mChains[chain]` with the lock released
  (`:310`) while another hand `push_back`s under it (`:285`). A deque's elements stay; its block map, which
  `operator[]` reads, does not. Take the reference under the lock (`:298` already does) and hand
  `const QueuedChain&` to `makeChain`; correct the comment at `contentpreparation.hpp:285`.
- [ ] **bug** `contentstore/store.cpp:349-371` with `misc/monitor.hpp:36-44`, and the same split at
  `store.cpp:249,289,323,398`, `preparedimages.cpp:120` — a task is pushed in `under(...)` and announced by a
  later empty `give([] {})`. Between the two the queue holds work while `mIdle` is true, so a second writer's
  `await(fits)` answers "no worker" and drops its entry (`:379`), and `flush()` can return with a task queued.
  `give` returns what its write returned, the push goes inside it, and the seven empty `give`s go: what a waiter's
  predicate reads is changed under the one lock hold that publishes it
  (https://en.cppreference.com/w/cpp/thread/condition_variable). A test with two writers and a small queue bound
  holds it, so the queue's bound becomes a `Policy` field here, ahead of the store's other bounds (Batch 5).
- [ ] **bug** `objectpaging.cpp:1169` (`ObjectStorage::getLight`) — `search` reads the store's dynamic map first
  (`mwworld/store.cpp:123`) on the cell reader thread (`cells/cellreader.cpp:254`), while the main thread clears
  it at every `World::clear`. `searchStatic`, as every sibling lookup there. Then hand `ObjectStorage` the store
  at construction and expose static lookups only. **Decided 2026-11-13: fixed in place**, in the fork's own
  lines of upstream's file, under the entry Batch 16 records for it. Refused: the lookups moved to a file of
  the fork's own; the one word alone, which leaves the guarantee a convention.
- [ ] **bug (unguaranteed)** `trace/bounce.comp:44-66`, `trace/visibility.comp:433-443` — a lane is elected and its
  answer broadcast after non-uniform control flow, which core Vulkan does not promise reconverges; no module
  declares `subgroup_uniform_control_flow`. A split tangle gives lanes a slot base of nought and appends a tile
  twice. Two extensions give the guarantee, and Roadmap 2024 requires both
  (https://docs.vulkan.org/spec/latest/appendices/roadmap.html):
  `SubgroupUniformControlFlowKHR` reconverges a subgroup that was uniform on entering a structured loop or
  selection and left by its merge block, which says nothing certain of an early return or a traversal loop
  (https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_subgroup_uniform_control_flow.html);
  `MaximallyReconvergesKHR` reconverges "as soon as possible", by the control flow graph, as the source reads
  (https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_maximal_reconvergence.html). The mode is
  one `OpExecutionMode` an entry point, so the pinning step, which already adds `SignedZeroInfNanPreserve`,
  can add it to every module that uses a subgroup operation: the build's guarantee, as the float arithmetic is.
  **Decided: `MaximallyReconvergesKHR`**, required at device creation beside the 32-lane requirement
  (`VK_KHR_shader_maximal_reconvergence`), held by a module check, and `./omw kernels` names what moved.
  Refused: uniform control flow, whose text does not cover what these stages do before they elect; lane 0
  appending after a `barrier()` over a shared OR, a barrier a workgroup in two hot stages and another list
  order.
- [ ] **bug** `cells/templatewalk.cpp:181`, `cells/prepared.hpp:166`, `cells/held.hpp:38`, `cells/cellplacer.cpp:514`
  — the ring never carries `MeshInstance::mClockwise` (set only at `sceneextractor.cpp:944`), so a static whose
  model states clockwise winding stands flipped in the distance and flips as its cell loads. One maker of a
  placement's chain-derived facts, shared by the walk and the reader; `Stood` holds it. A test with such a model
  in both paths.
- [ ] **bug** `shaders/texture/bc7encode.comp:309-320` — the power iteration starts from the box diagonal, not
  "the widest channel's direction" its comment states. Where the deviations are orthogonal to the diagonal
  (eight texels of (200,100,50) and eight of (100,200,50)), `covariance * axis` is nought and the block is stored
  as its mean. Start from the covariance's column of the largest diagonal entry; a two-colour block in
  `RtxBc7EncodePassTest`. Line 324 names a local `length`.
- [ ] **bug (low)** `shaders/trace/denoise/accumulateclamp.comp:156-158` — the clamp's 5×5 mean reads raw samples
  where the accumulator blends held ones (`heldSampleShare`), so one firefly sets the push's direction. The same
  hold, or the held sample handed on.
- [ ] `scene/bottomlevelstore.cpp:155,191-195` — **bug (latent)** a deforming mesh with no triangles is left
  `mUpdatable` with no structure. Set it where the structure is made.
- [ ] **bug** `rtxvulkan/device/memory/slottable.cpp:409-412`, `texture/texturearray.cpp:113-116` — "the other copy
  waits for nothing" is asserted after the hold was released, so it cannot fail for its reason. Ask before
  `releaseAfter`; drop the 200 ms sleeps for `Timeline::hasFinished`, as `readstamp.cpp` does.
- [ ] `./omw shot --views=all --map --upscale=off --out=<dir>` and `./omw kernels > <file>` once the batch is in:
  the pictures and the kernels every structural item after it is held to. A **measure** item that moves either
  says so and takes them again.

## Batch 3 — The tests

Files: `apps/components_tests/{rtx,rtxvulkan,myguirtx}/**`, `cmake/Tests.cmake`, `components/testing/*`.

Ahead of the batches whose files these tests hold (Order). The GPU binary's shards are Batch 0's.

- [ ] `rtx/sourcetree.cpp:172-272,396-441` — ten of fourteen rules pass when their matcher matches nothing; four
  allow-list entries are dead (`vkDestroyInstance`, `vkDestroyDevice`, the messenger, `owned.hpp`). Each rule
  asserts its pattern and every allow-listed entry matched.
- [ ] `rtx/sourcetree.cpp:473-481` — no rule holds `#pragma once`, preprocessor conditionals, or vendor identity;
  `sFolderOrders` omits `myguirtx`, `contentstore`, `mwrender/rtx`, `rtxbuild`; the backend's reach into the core
  past `renderer/` is unchecked (`trace/wavepass.hpp:10-11` includes `environment/`); `docs/architecture.md:151-164,315-327`
  lists another order than the test. Rows in one rule table; the doc checked against it; the wave model to
  `world/`.
- [ ] `rtx/sourcetree.cpp` — fourteen tests each walk and read the tree, some ten thousand opens. One in-memory
  tree in `SetUpTestSuite`, the include closure memoised.
- [ ] Shared support: an `osg::Image` builder (some fifteen local ones over ten files); a `SubjectWalk` bundle
  (22 hand-built triples); `BoneClock`, `Throwing`, fixed `FileStamps` copied; `allocationsIn(fn)` for 22
  before/after pairs; `Testing::runProbe` for six copies of the probe-kernel scaffold; `CountingRenderer` over
  `NullRenderer`, not a second implementation of 46 overrides.
- [ ] `components/testing/util.hpp:31-58` — 43 `outputFilePath` calls and 48 manual removes beside
  `currentTestDirPath()`, which `FreshTestDirs` empties. One route for the fork's tests.
- [ ] Death tests re-execute the binary: 35 in the core's suites (eight in `scene/scenedesc.cpp:1795`), 21 in the
  GPU binary, each child building the validated harness. One death a contract; no layers in a child.
- [ ] `rtxvulkan/present/presenter.cpp:69-88` — an instance, a surface and a device with the suite's pipeline cache
  per test, in a folder that compiles no pipeline. `SetUpTestSuite`, an empty cache spec.
- [ ] `rtx/common/parallel.cpp:139`, `rtx/mirror/cells/cellring.cpp:1409,1950,1982,2019,2068` — six waits with no
  `WaitBound`; `parallel.cpp:142-146`, `job.cpp:122-125` elapsed time as the oracle. Bounded, and proved by state.
- [ ] `rtx/world/moon.cpp:72-75,136-141,412-429` — six bands for an order dependence the fallback seed removed.
  Exact values.
- [ ] `rtx/frame/bluenoise.cpp:43-66` — a naive 2D transform, 33.6 M `std::polar` calls in one test. A 64-entry
  root table.
- [ ] `rtx/world/fogbuilder.cpp:408,520-526` a test that asserts and mutates a global its subject cannot read;
  `rtx/support/geometry.hpp:206,214` function-local static scratch shared by every user;
  `support/device/harness.hpp:100` and three more comments with stale sizes and shard counts.
- [ ] Gaps: no allocation guard in the host binary for `SceneUploader`, `FrameDescriber`, `CompositeQueue`;
  `rtx/image/chainbuild.cpp:500-646` ten digests over whole chains that say only "differs"; the hosted harness
  files (some 3.8k lines) built into the executable alone.

## Batch 4 — Worker threads and the monitor

Files: `components/misc/monitor.hpp`, `components/contentstore/store.cpp`, `components/rtx/prepare/contentpreparation.{hpp,cpp}`,
`components/rtx/preprocess/preparedimages.{hpp,cpp}`, `components/rtx/common/parallel.hpp`,
`components/rtxvulkan/trace/visibilitypass.cpp`, their tests.

- [ ] `contentpreparation.cpp:226-339`, `preparedimages.cpp`, `store.cpp` — three pools: two on `Monitor`, one on
  a raw mutex with a predicate-less wait (`:251`); `Hand` and stop-all-then-join-all written three times. One hands
  helper on `Monitor`, the stop through `condition_variable_any::wait(lock, stop_token, pred)`.
- [ ] `contentpreparation.cpp:298-305` — a log line and `PreparedImages::admits`, which asks the system's memory,
  run under `mMutex`. Outside the lock.
- [ ] `preparedimages.hpp:141` — `mQueue` states no bound and is FIFO with no supersede, each job pinning its
  file's pixels. A bound in bytes, and what happens at it.
- [ ] `preparedimages.cpp:171-224` — play's chain hands never call `letGoChainScratch`; one miss on a 4096 map
  leaves a hand holding about 106 MB for the session. Let go when the queue drains, as the preparation's hands do.
- [ ] `preparedimages.hpp:97-115` — entries keyed by `const osg::Image*` plus two observers and a tuple read with
  `std::get<3>`. A named key by what names the image (`ChainIdentity`).
- [ ] `visibilitypass.cpp:317,341` — a lambda that lives as long as the object captures the constructor parameter
  `&textureLayout`; its two siblings are members. Store it.
- [ ] `visibilitypass.cpp:328,344,395` — "stopped" travels as a bare `std::runtime_error` through `Job::wait`, and
  the tuple compile polls `waitFor(100 ms)`. A stop reported through `JobProgress`; a wait on the job.
- [ ] **measure** three pools each sized against the whole machine (prepare N−1, compile N, chains N/2 parked for
  the session). One budget split by their owner. Decide by the cold start's wall time with both running.

## Batch 5 — The content store and the preparation

Files: `components/contentstore/*`, `components/rtx/prepare/*`, `components/rtx/preprocess/**`, `components/rtx/image/*`,
`apps/components_tests/{contentstore,rtx/prepare,rtx/preprocess,rtx/image}`.

- [ ] `contentstore/store.hpp:98-115`, `store.cpp:506-514` — the queue, resident and stamp bounds are constants,
  and a holding store reserves both tables at them ("some 80 MiB") in every session and every test. `Policy`
  fields, with a count bound beside the byte bound. This also answers the open issue of
  `ContentStoreTest.aFileThatDoesNotMatchIsAMissAndIsDeleted` at 1.20 s.
- [ ] Tests on the above: 47 sites construct a holding store; `contentstore/store.cpp:346-364` builds 128 MiB of
  payload to reach a 64 MiB bound; the full-queue drop, the drop while stopping, the resident cap and the stamp
  cap have no test. Small bounds, and the four paths.
- [ ] The open issue of two cold stores holding different entries: read `mKind` in the differing files' trailers,
  diff the file list at "Prepared" against exit, read `dropped` and `missed`. Candidates: a shape keyed by
  `mBeside` of a clone in play; the lost write Batch 2 fixes; a frame-thread write dropped at a full queue.
- [ ] `store.cpp:519-537` — the start sweep learns an entry's kind by reading it whole. The layout Batch 1
  decided: the kind in the path, or the trailer read first.
- [ ] `store.cpp:230-235,318-321,445-456` — every first hit queues a `utimensat`, though the time ranks only small
  entries of unheld kinds and nothing evicts. Stamp only what ranks.
- [ ] `store.cpp:179-186` a `Task` with a kind tag and per-kind fields, stamps escaping the byte bound (a variant;
  stamps coalesced); `store.cpp:419-431` `keepResident` refuses past its cap with no stat; `store.cpp:80-93`
  `holds` reads through `ifstream` where every other read is `Platform`'s, under a doc that says "one `stat`".
- [ ] `preprocess/texture/chainpass.cpp:62-198`, `conversionpass.cpp` — what a run makes of a header is derived
  three times per pass, marked "As `run` builds". One plan struct.
- [ ] `image/chainbuild.cpp:217,261,326,475,542-553` — alpha decoded whole, then copied into every texel's fourth
  byte, later readers using the plane (17–22 MB a 4096 build, per thread). Decode in the band walk, read from the
  texels.
- [ ] `preprocess/contentcache.hpp:64-77` — `keep` copies the made chain into a fresh payload for a stored ask
  nobody reads. `encode(MadeChain&&)`.
- [ ] `image/chainbuild.cpp:235`, `alphaimage.cpp:180`, `conversionpass.cpp:176`, `imagedescription.cpp:348` —
  `vector<std::byte>` filled then wholly overwritten, where `Misc::ByteBuffer` is the fork's buffer for it.
- [ ] `prepare/contentpreparation.cpp:208-224` — the manifest's signature covers the passes and not the walk's own
  rules, behind a hand-bumped number. A walk version held by a test that digests a fixed model's keys.
- [ ] **measure** `prepare/preparationmanifest.cpp` `read` — a 9 MB manifest parsed at every start into a deque of
  strings and three node maps. Flat tables over the read buffer.

## Batch 6 — The mirror and the ring

Files: `components/rtx/mirror/**`, `components/rtx/scene/meshtable.*`.

- [ ] `cells/cellplacer.cpp:436,463` — a `stable_sort` of 128 B placements, with its heap buffer, on the adoption
  frame. Sort `PreparedCell::mRefs` by radius on the reader thread.
- [ ] `cells/cellplacer.hpp:306`, `.cpp:61-89,469` — `mMoved` gains an entry for every reference the game ever
  moves and is cleared only by `forgetReferences`; each adoption walks all of it. Drop entries with no home, and
  look up the adopted cell's references.
- [ ] `chainkeys.cpp:56-98` — a pair's liveness is inferred from `referenceCount()` less its occurrences in the
  release sink; the one identity table that is not a `Kept`. Counted holds. **measure** `ChainKeys::retire` on the
  `streaming` suite.
- [ ] `cells/templatewalk.hpp:66-100` — `TemplateWalk` owns the thread's `ThreadContent` and forwards eight of its
  methods. The thread's owner holds it and hands it at `read`.
- [ ] `cells/cellreader.hpp:174` a `flat_map<RefId, Normalized>` that shifts strings at each insert
  (`boost::unordered_flat_map`, as `CellHolds`); `meshreader.cpp:459` `gatherBeside` walks the model per drawable
  (cache beside `mFacesOf`); `materialresolver.cpp:548` hashes a path the slot's facts already answer;
  `cells/cellsupply.cpp:198` measures every pooled model after every cell (on ask);
  `sceneextractor.cpp:376,347` asks `asSwitch()` and `jumps()` twice.
- [ ] `cells/prepared.hpp:231-238` — a lent model keeps a float copy of its geometry that `adoptParts` reads once
  (34.9 MiB vanilla, 83.5 MiB on the large profile, by the bench records). Free after first adoption; re-read from
  the template in the rare second case. **measure** how often a model is adopted twice over a crossing.
- [ ] **measure** `cells/cellplacer.cpp:382`, `cells/groundreader.cpp:119-120` — every landed cell adds its own
  copy of the grid's 24,576 indices and 4,225 texcoords, identical for all. One shared run each. Count landed
  cells held at the default reach first.
- [ ] **measure** `sceneextractor.cpp:333-355`, `frozenroots.cpp:81-110` — a still frame polls every reference
  root: about ten cold lines each. Compare position, attitude and scale directly and compose only for walked
  roots; frozen faces as a run per cell root in child order. `./omw profile` by line at a still place under the
  large profile decides.
- [ ] **measure** `cells/held.hpp:41,77-100` — `Stood::mTransform` (64 B a part) is derivable and stored again in
  the scene row. Count placements held per cell.
- [ ] **measure** `sceneextractor.cpp:360` a formatted crash note per walked root per frame;
  `sceneextractor.cpp:389-393` a lamp's root walked every frame for a light that does not move.

## Batch 7 — The scene's tables

Files: `components/rtx/scene/*`, `components/rtx/common/{runs,slots}.*`, `components/rtx/renderer/{renderer.hpp,nullrenderer.cpp}`,
`components/rtxvulkan/scene/scenebuffers.cpp`, `components/rtxvulkan/vulkanrenderer.cpp`.

- [ ] **measure (read)** `scene/placementtable.cpp:166-173` — `jump` returns before setting `mPrevious` when `move`
  reports no change, so a slot moved by a walk that handed nothing over would report motion across a jump. Trace
  whether a skipped hand-over can precede a jump notice; if so `jump` always sets `mPrevious`.
- [ ] `scene/placementtable.cpp:219-230`, `scene/instancerecord.cpp:198-205` — `advance()` settles every touched
  slot, so a slot only placed, dropped or re-worn is recomputed (a double 4×4 inverse) and its 160 B row owed to
  both copies again a frame later; the frame after a `setScene` does it for every live slot. Settle only slots
  where `mPrevious != mTransform`; the settle pass then writes `mMotion` alone.
- [ ] `scene/instancerecord.cpp:104,168` — a moving slot's matrix is inverted in `recordOf` and again in
  `moveRecord`. One inverse, handed on; water keeps its own.
- [ ] `scene/instancerecord.hpp:35-122` — an `InstanceRecord` (about 176 B) kept for every slot, derived wholly
  from its row. After the two items above, scratch for changed slots. Confirm `placeCompacted` needs only the
  structure reference.
- [ ] `renderer/renderer.hpp:575-598` — six pure virtuals, each one field of `RenderProfile`, both backends
  repeating the asserts. One `apply(const LiveSettings&)`.
- [ ] `scene/texturetable.hpp:138-160` — `add`, `addSpriteLight`, `addGround` answer a live slot with no hold,
  beside `takeTexture`'s `TextureHold`. Every adder answers a hold.
- [ ] `common/slots.hpp:21-23` — `SlotPool` stands apart for a reason that no longer holds (`SlotRows::take`
  moves); four tables hand-roll "take or append". `SlotRows<T>`.
- [ ] The five tables spell one lifecycle five ways (`reserve(size_t)` against `reserve(const SceneRoom&)`,
  `isLive`/`isPlaced`, `hold`/`drop` against `stand`/`release`, three shapes of news). One protocol on `HeldRows`.
- [ ] `renderer/renderer.hpp:536` a nullable `mSky` asserted non-null (a reference parameter);
  `scene/scenedesc.hpp:182` `addEmitter` with seven parameters and two trailing bools (a struct).
- [ ] The host geometry mirror, as Batch 1 decided: freed after arrival, or its header saying why it stands.
- [ ] **measure** `scene/materialtable.hpp:49-51`, `scenebuffers.cpp:163`, `cells/groundreader.cpp:180` — blend
  weights are bytes in the content and floats in the reader, the host run and a device table reserved at 32 MiB.
  Bytes, four to a word. `./omw kernels` and a bench.
- [ ] **measure** `scene/meshtable.hpp:144`, `scenebuffers.cpp:202,211-220` — a tangent word per vertex, zero for
  every mesh with no normal map, on host and device. Runs of their own, as `mSecondRuns`. Read the vertex count
  from a scene report.
- [ ] **measure** `common/runs.cpp:23-70` — a linear best-fit scan and a `vector::erase/insert`, on "tens of
  entries" nothing reads. Report `getHoleCount`'s high-water mark; past the hundreds, a segregated-fit allocator.
- [ ] **measure** `sWorldRoom`'s `1<<18` placements against some 80k used; `GpuInstance::mMotion`, 48 of 160 B,
  the identity for nearly every row. Bytes at the three contents first.

## Batch 8 — The device layer

Files: `components/rtxvulkan/{device,pipeline,spirv}/**`.

- [ ] `device/commands.hpp:28-255` — `CommandPool` is also the staging allocator; the file holds five classes, and
  `device/` and `device/memory/` include each other through it. `memory/staging` and `memory/batch`.
- [ ] `device/commands.hpp:28`, `device/timeline.hpp:87`, `device/readstamp.hpp:66` — single-thread by a comment,
  reached through a `const Device&` compile hands also hold. `OwnedBy`, checked in `take`, `begin`, `defer`,
  `submit`, `waitFor`, `next`.
- [ ] `device/device.cpp:349-592,635-748` — `Device` owns pipeline statistics and code capture, and fault and
  marker description. `PipelineReport` and `FaultReport`.
- [ ] `device/memory/buffer.cpp:48-51` — `sReferenceAlignment` restates seven `buffer_reference_align`s by hand.
  One shared constant each is asserted against, or a check at `ShaderCode::stage`.
- [ ] `device/pipelinecache.cpp:192-194,231` — the body copied out of the file buffer and digested, then digested
  again in the constructor. `read` answers the buffer, the body's span and its digest.
- [ ] `device/handles.hpp:118-160`, `pipeline/computepipeline.hpp:62-70` — a pipeline layout and set layout per
  pipeline, some 430 pairs where about nine differ. Owned by the kernel row.
- [ ] `spirv/spirvinterface.cpp:54,349` — facts keyed by id in an `unordered_map` though ids are dense, and every
  function body walked though declarations end at the first `OpFunction`. A vector by the bound; stop there.
- [ ] `pipeline/tracepipeline.cpp:122` a `const traceRays` that flips a `mutable` flag to upload on first launch
  (upload when the kernel is adopted); `device/gputimer.cpp:92,121` queries reset two ways (both from the host);
  `device/memory/barriers.hpp:75-77`, `pipeline/dispatch.hpp:203-205` a few KB value-initialised per pass.
- [ ] **measure** `device/commands.cpp:544`, `device/result.hpp:47`, `vulkanrenderer.cpp:446-450` — a world build
  stages every texture and flushes in one submit, behind a fixed 10 s wait that ends in `deviceFailed`. Flush at a
  staged-byte budget. Staging's high-water mark and the submit's length at a PBR start on the weakest card.
- [ ] **measure** `device/memory/memory.cpp:36,387` the 64 MiB block and 32 MiB dedicated threshold carry no
  reason (count dedicated allocations under the PBR stack); `spirv/spirvpin.cpp:642` per-result decorations where
  `SPV_KHR_float_controls2` offers a default.

## Batch 9 — The backend's scene, acceleration and textures

Files: `components/rtxvulkan/{scene,texture,trace}/*`, `components/rtxvulkan/device/requirements.cpp`,
`components/rtxvulkan/shaders/lib/traversal.glsl`.

- [ ] **measure (read)** `texture/texture.cpp:683-692,866-872` — `setAnisotropy` buries the old samplers and only
  `sync`, inside `place`, pays the sets; a trace of a scene unplaced since would bind samplers the graveyard
  destroys, guarded by a debug `assert`. Show whether such a frame exists; if so `setAnisotropy` pays.
- [ ] `scene/bottomlevelstore.cpp:159-169` against `scene/sceneacceleration.cpp:288-290` — a deforming mesh's build
  flags are spelled twice, held together by "with the same flags". One `flagsOf(deforms)`.
- [ ] `scene/bottomlevelstore.hpp:126-145`, `sceneacceleration.cpp:173-303` — the refit's state in the store's rows,
  its logic in `SceneAcceleration`, seven accessors between. `BottomLevelStore::prepareRefit` owns the rota, the
  scratch and the build infos.
- [ ] `scene/sceneacceleration.hpp:118-133`, `scenebuffers.hpp:81-89` — indices and poses in one class, the other
  streams in another, "because the build had to have them first"; three mutable accessors feed `SkinPass`. One
  `SceneGeometry`, declared first.
- [ ] `scene/devicescene.cpp:83-91,132,160` — freed meshes are pulled (`releaseFreed`), freed textures pushed
  (`dropTextures`), under two name pairs. One route, one pair.
- [ ] `texture/texture.hpp:386-397`, `texture.cpp:297-341` — eight samplers, the neutral texel and the stand-in
  made per scene, though they depend on the device and the anisotropy. They live in `ScenePasses`.
- [ ] `trace/tracemedia.hpp:66-72`, `trace/wavepass.hpp:42`, `FogTile::describe` — production API only a test probe
  calls; the game's `SeaState` is one value. Test reach-ins or constructor parameters.
- [ ] `scene/sceneslots.cpp:65,87` two uncommented `const_cast`s; `texture/texture.hpp:342-355` three `mutable`
  scratch vectors behind a `const chooseLevels`; `bottomlevelstore.cpp:229-257` and `texturearrival.cpp:127-141`
  the same split-into-runs twice (one helper in `common/runs.hpp`); `texture/texture.hpp:222-224` a doc that
  says slots are owed where samplers are.
- [ ] **measure** `scene/bottomlevelstore.hpp:70`, `texture/texturearrival.hpp:24` — the build scratch and the
  spread means grow to a world load's mark (up to 64 MiB each) and stand for the session as essential memory.
  A load hands back what it grew past a streamed arrival's mark. Read `getScratchBytes`/`getMeansBytes` after a
  load at the three contents.
- [ ] **measure** `scene/sceneacceleration.cpp:570-579` — the top level is updated in place for any run of frames
  that only move, with no bound on the chain; the recorded gain is from legs where cells keep arriving. NVIDIA:
  "For TLAS, consider the PREFER_FAST_TRACE flag and perform only rebuilds", since a top level should be "as
  high quality as possible regardless of the movement"
  (https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/). Hold still five
  to ten minutes in an interior with walking actors and A/B "always build": `tlas`, `trace`, `direct`,
  `bounce`. A whole build was 3.45 ms on the handheld, so a rota is a spike the frame-time rule refuses: if the
  decay is real, the bound is a build on a frame whose shape changes anyway, or after the moved share of
  instances passes a mark; if it is nil, the header records that.
- [ ] **measure** `scene/bottomlevelstore.cpp:157-160`, `traversal.glsl:272-277,498,722-744`,
  `device/requirements.cpp:32` — a candidate's corners come off the structure by position fetch, a committed
  hit's off the tables. One route removes `ALLOW_DATA_ACCESS` from every bottom level and a start requirement.
  Build sizes with and without the flag (a host query), then the trace zones at the ship.
- [ ] **measure** `scene/skinpass.cpp:60-116` one push and dispatch per posed mesh; `bottomlevelstore.cpp:159`
  `PREFER_FAST_TRACE` on bodies that are rebuilt on a rota; `scene/toplevelpackpass.hpp:14-16` a compute pack in
  place of inactive rows, with no recorded figure. Each by its zone at a crowd; the pack's header takes its number
  or the pass goes.

## Batch 10 — Surface transport shaders

Files: `components/rtxvulkan/shaders/trace/{bounce,visibility}.comp`, `shaders/lib/{traversal,shading,lights,peel}.glsl`,
`components/rtxvulkan/device/requirements.cpp`, `components/rtxvulkan/frame/gbuffer.cpp`, `apps/rtxtool/noise.*`.

- [ ] `lib/lights.glsl:295-299` — `terminatorShare` is worked out for every lamp in tuples with no maps; the
  float-equality select cannot fold under the pin. `HAS_MAPS` in front. Pictures stay; `./omw kernels` names the
  vanilla tuples.
- [ ] `trace/visibility.comp:407` — neutral constants stored to the pane channels on every pixel every frame
  (16–32 B a pixel). The channels join `mHoldsNothing` under "a see-through placement is in the scene".
- [ ] `trace/visibility.comp:283-297` and `trace/panes.comp:118-131` the medium in front of a point written twice
  (one `mediumAheadOf`); `lib/lights.glsl:537,619,647` the cell's run selection three times (one `lampRunAt`).
- [ ] An exact mode for the harness: level-0 cutouts, a per-channel lamp estimate, no rates, unfiltered. It makes
  the three items below numbers.
- [ ] **measure** `lib/traversal.glsl:363-385` — a bounce ray tests a cutout at its cone's level with the
  rasterizer's coverage scale (`1 + 0.25·lod`), so a leaf card is wholly solid or wholly absent to it. Two
  forms: meet by chance of the filtered alpha, on rays that do not draw, whose expectation is the coverage
  (Enderton et al. 2010, which `cutAt` cites); or coverage kept in the chain itself as the preparation builds
  it, each level's alpha scaled until the share of texels passing the reference is the finest level's
  (Castaño, https://www.ludicon.com/castano/blog/articles/computing-alpha-mipmaps/; DirectXTex's
  `ScaleMipMapsAlphaForCoverage`), which replaces the runtime scale for every ray but still leaves a one-texel
  level all or nothing. Neither is the declined hashed alpha: that was a cut fixed to the surface, and a draw
  each frame is what `cutAt` already does for a soft edge.
- [ ] **measure** `lib/shading.glsl:1327-1347` — a diffuse landing evaluates its GGX lobe at authored roughness
  toward the bounce, guarded by the firefly ceiling alone (PBR content). Regularize at `PATH_INDIRECT`: a
  roughness floor after a rough bounce, as Cycles' Filter Glossy does
  (https://docs.blender.org/manual/en/4.1/render/cycles/optimizations/reducing_noise.html), which keeps the
  energy a clamp drops (https://link.springer.com/content/pdf/10.1007/978-1-4842-4427-2_17.pdf) and adds a
  bias the floor's size sets (Weier et al. 2021,
  https://diglib7.eg.org/bitstream/handle/10.1111/cgf14347/v40i4pp139-151.pdf).
- [ ] **measure** `lib/shading.glsl:970-1049`, `lib/lights.glsl:392-406` — the lamps' one scalar fraction is biased
  where lamps differ in colour or occlusion, and with a lobe it multiplies the diffuse sum alone. Keep it or carry
  three channels, once the exact mode gives the size.
- [ ] **measure** spatiotemporal blue-noise masks for the 64² tile; uv and colour as one stream and normal and
  tangent as one; the histogram of lamps a cell under the large profile.

## Batch 11 — Reconstruction and display

Files: `components/rtxvulkan/shaders/{texture,trace/denoise,upscale,display}/*`, `components/rtxvulkan/{denoise,upscale,display}/*`,
`docs/architecture.md`, `docs/research/2026-11-11-ray-reconstruction-against-super-resolution.md`.

- [ ] `upscale/upscaler.cpp:79-153` — nine methods branch on the variant's kind because `FsrUpscaler` lacks the
  operations. Each backend answers the same set; `std::visit` throughout.
- [ ] `denoise/denoisepasses.hpp:80`, `denoise/atrouspass.hpp:77` — two public functions over a private one that
  takes a nullable selector and returns an `optional` each caller asserts on. The `if` goes up.
- [ ] `denoise/atrouspass.cpp:98`, `denoise/compositepass.cpp:38` two identical stand-in images (one, handed);
  `upscale/fsrupscaler.cpp:305-310` a constant commented "as the SDK clears it" that differs from the 3.1.4 host
  (say why, after re-reading the SDK file).
- [ ] `docs/architecture.md:408` — three refusals for Ray Reconstruction cite a note that holds no row for them,
  measured on content with no lobe. NVIDIA's guide lists specular motion vectors, or a specular hit distance
  with the world-to-view and view-to-clip matrices, among the buffers Ray Reconstruction requires (4.1.8,
  4.1.9), and the colour before transparency as optional
  (https://github.com/NVIDIAGameWorks/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md). Run `noise --ab`
  under the PBR profile with a hit distance and both matrices, and record its rows in the note.
- [ ] **measure** `shaders/upscale/fsrcallbacks.glsl:516-521` — the reprojection runs the reference Lanczos, at
  least 16 `sin` an output pixel, where the SDK's host binds a 128-entry table on 32-to-64-lane devices. Add the
  table, chosen by a subgroup-size capability; no vendored line changes. `./omw kernels` and the Upscale zone.
- [ ] **measure** `shaders/lib/surfacematch.glsl`, `trace/denoise/{specular,pane}.comp` — bilinear history for
  two filters with no spatial pass behind them; ReLAX takes Catmull-Rom where the footprint is whole. `noise
  --strafe --walk` under the PBR profile.
- [ ] **measure** `display/exposurepass.cpp:89`, `shaders/display/histogram.comp` — every output pixel binned
  every frame, with no Exposure column in any table. A quarter of the pixels; `shot --against`.
- [ ] **measure** the glossy and pane means as one persistent image plus arena scratch; the shadow mask and
  temporal passes once for both fields; the bloom pyramid in `R11G11B10F`.

## Batch 12 — Fog, sprites, water and sky

Files: `components/rtxvulkan/shaders/{trace,lib,scene}/*` (fog, sprite, water, ripple, wave), `components/rtxvulkan/trace/*`,
`components/rtx/world/fogbuilder.cpp`, `apps/components_tests/rtx/sourcetree.cpp`.

- [ ] `trace/spritebin.cpp:67` — `mPuffsInFrame` is set by any sprite, so one additive flame makes every froxel
  cast an ambient ray nothing reads (`fogscatter.rgen:179`). Count what `puffLight` is asked for.
  **measure** `air` at the guild.
- [ ] `lib/fog.glsl:12-23`, `trace/spriteemitters.rgen:6-10`, `lib/medium.glsl:34` — `fog.glsl` holds the field
  and the lit volume, so a pass that casts no ray is a ray-tracing launch; `medium.glsl` includes the whole sprite
  march. `fogfield.glsl` and `puff.glsl`.
- [ ] `trace/fogdepth.rgen:43-49`, `trace/spritecomposite.rgen:65-66` "where the eye's view ends" at three sites
  (one function); `trace/spriteemitters.rgen:41-52` a texture's mean alpha worked out per emitter per frame (at
  arrival, with the sprite light bake).
- [ ] What Batch 1 decided of the lamps' phase and of precipitation in the self-shadow, each landed without its
  switch or removed with it.
- [ ] **measure (content)** `lib/fog.glsl:128-131`, `world/fogbuilder.cpp:129-146` — a room's haze falls off from
  world z = 0 or the cell's water level, so a room authored high holds a fraction of the air of one at nought.
  Scan interior heights and shoot a high interior; then a base from the cell, or a room even in height.
- [ ] **measure** `trace/fogscatter.rgen:101-111,169-181` — a froxel past every surface still fetches and casts.
  A per-column farthest depth from last frame's block, dilated, dropped on a cut. Count such froxels, then `air`
  and `noise --strafe --cut`.
- [ ] **measure** the five ray-generation passes as compute dispatches (`sourcetree.cpp:352-390`). The stated
  cause is a dispatch preempted with a traversal in flight, "a candidate counted twice or not at all";
  `NO_DUPLICATE_ANY_HIT` answers the first half and not the second, and the trace's own stages are dispatches
  held by `repeat`. `repeat --pairs=10`, quiet and contended, with the fog passes as dispatches; the rule stands
  unless every pair agrees.
- [ ] **measure** `scene/spriteshade.comp:274-293` two barriers a sprite across 1024 lanes (per-lane accumulation,
  the same sums in the same order). `shade` under snow, after Batch 1 says whether snow is shaded at all.
- [ ] **measure** `trace/media.comp:64,116,127` three traversals an earlier stage already traced (three floats
  beside the stage record); `trace/spritecomposite.rgen:181-193` every traced pixel visited for a few tiles;
  `trace/ripplestep.comp:81-82` every texel against every impulse; `trace/tracemedia.cpp:34` the sea synthesised
  inland; `lib/fog.glsl:107-122` field fetches in a room whose coverage is one.

## Batch 13 — The frame, presentation and the interface

Files: `components/rtxvulkan/{gui,present,display,frame}/*`, `components/rtxvulkan/{vulkanrenderer,framering}.*`,
`components/myguirtx/*`.

- [ ] **measure** `gui/guidrawer.cpp:40` — `prepare` waits on the host for the slot's last submit. After an
  unshown present (`mDrawn` not bumped) the same slot is taken again, whose newest stamp is this frame's own
  placement or trace, so the wait would last a device frame: every frame of a resize drag. Read the frame's
  host wait under a drag; if it shows, the slot advances on every prepare.
- [ ] `vulkanrenderer.cpp:83-103,245-356` — `createTargets` does five jobs and `frameAt` is a second list of the
  same parts. A `FrameTargets` owner with `bytesAt` and `resize`.
- [ ] `myguirtx/texture.cpp:73,122` — a narrow texture's `mPixels` lives as long as the texture and is read only
  between `lock` and `unlock` (2 MiB a 1024² atlas). Allocate in `lock`, release in `unlock`.
- [ ] `display/displaychain.cpp:115,138` the composed frame copied whole for a test's read (in place unless read
  back); `frame/gbuffer.hpp:201` a `vector` of fixed size its comment calls an array;
  `myguirtx/rendermanager.cpp:216-217` `resize` then `memcpy`; `myguirtx/texture.cpp:137-160` a switch inside the
  per-pixel loop.
- [ ] `present/swapchain.cpp:192` — recreation is destroy-then-create behind a drain, with
  `swapchain_maintenance1` enabled for its fences alone. `oldSwapchain` and a present-mode switch
  (https://docs.vulkan.org/samples/latest/samples/api/swapchain_recreation/README.html). Low at this frequency.
- [ ] `gui/guidrawer.hpp:57` — the comment says "Host-visible device memory… no staging copy"; `prepare` stages.
  Correct it.
- [ ] `docs/architecture.md:355` — "a shared device copy (Unreal's GPUScene)" is refused with no reason beside
  it. The reason is the landed proposal's: one copy changes every debt and makes each write wait out the frame
  before it on the queue, and a frame reads the previous pose copy for motion. Say so there. The copy a frame
  slot stands, for the scene's tables and the interface's vertices alike.
- [ ] Stale narration: `display/exposurepass.hpp:63`, `sunglarepass.hpp:49` ("host memory");
  `trace/tracerecording.hpp:77`, `bloompass.hpp:25`, `frame/gbuffer.cpp:63` (a `readComposite` only a test probe
  has); `frame/framestep.hpp:12`; `frame/gbuffer.cpp:35`; `gui/guipass.cpp:139` ("the four", of five);
  `displaychain.cpp:107-113`.
- [ ] **measure** `gui/guitextures.cpp` (`add`), `myguirtx/slottexture.cpp` (`fillWith`),
  `myguirtx/rendermanager.cpp` (`getTexture`) — every file-backed interface texture is decoded to RGBA8, counted
  essential and never dropped: 4 MiB a 1024² splash seen. Block formats kept, and residency by draw recency.
  Count interface texture bytes after ten loads.
- [ ] **measure** `present/presenttarget.hpp` (`mShown`), `present/presenter.cpp` (`hand`) — after the curve every
  output pixel is drawn into `mShown` and blitted to the swapchain. Draw into the swapchain image; a windowless
  renderer then needs a second route, which decides it.

## Batch 14 — The game side and the seam

Files: `apps/openmw/mwrender/{objectpaging.cpp,renderer.{hpp,cpp},glrenderer.cpp}`, `apps/openmw/mwrender/rtx/*`,
`apps/openmw/mwinput/rendererkeys.cpp`, `apps/openmw/engine.cpp`, `components/rtx/renderer/frameimage.cpp`.

- [ ] **measure (read)** `rtx/tracedterrain.cpp:313-317` — a template drawable shared with reader threads is
  const-cast and handed to the game's intersection visitor, whose pose hook runs a cull on it. Settle whether a
  ring placement can hold a `MorphGeometry`; if so the carrier answers without the pose hook.
- [ ] `renderer.hpp:20,23,112,448`, `renderer.cpp:333-361` — the seam names `DlssLibrary::Folders`,
  `Rtx::UpscalerChoice`, `Rtx::Unsupported`. Start and failure wording move to `rtx/`; the spec carries neutral
  paths.
- [ ] `renderer.hpp:152`, `rendererkeys.cpp:22-41` — `Renderer` is a `GraphicsListener` and `RendererKeys` a
  second one wrapping it; the direct route loses the F3/F4 message. `Renderer` stops inheriting it.
- [ ] `renderer.hpp:285-314,383-407`, `renderer.cpp:115-238` — frame pacing, eight members and four fields, in the
  interface's base. A `FramePacer` the engine owns, handed the renderer for `awaitPresented`.
- [ ] `rtx/rtxrenderer.cpp:576-621,715-742`, `frameimage.cpp:50-94` — a freeze, screenshot or thumbnail reads the
  frame into `mReadBack` (never freed, 33 MB at 4K) and copies it into a second full-size image; the thumbnail
  averages column-outer over a row-major frame through a `vector<double>`. One buffer released after, rows outer;
  the three members and four methods become one frame-reader part.
- [ ] `rtx/rtxrenderer.cpp:768-774` — `notifyWorldspaceChanged` skips the stretch close `notifyCut` does. One
  close. **Decided 2026-11-13: a rest is told as a cut.** Upstream's `worldimp.cpp:826` tells a jump of time
  as a worldspace change; it becomes a cut, recorded in `.notes/UPSTREAM.md`. `RenderingManager`'s notice of a
  changed worldspace is also what clears the effect manager there (`renderingmanager.cpp:897`), so the rest
  keeps that clear by a notice of its own that says both, and a test holds that an effect does not outlive a
  rest. Refused: upstream's line left, the renderer dropping the eye's adaptation and the wake at every rest.
- [ ] `renderer.hpp:106`, `engine.cpp:695` — `RendererSpec::mResourceDir` is set and never read. Delete.
- [ ] `rtx/tracedground.hpp:62` holds `WorldMirror&` for `getRing()` alone; `worldmirror.hpp:211-216` unpacks
  `MirrorKnobs` into five members; `rtxrenderer.cpp:530-569` has the await-with-patience loop twice;
  `renderer.hpp:184` takes `const CellStore*` it never tests. Hold what is used, one member, one helper, a
  reference.
- [ ] `mwgui/settingswindow.cpp:306,531` — upscaler labels read back by position, right only while the offered
  list is a prefix. Looked up by choice.
- [ ] `rtx/rtxrenderer.hpp:158-159` a doc comment with no declaration; `docs/architecture.md:107` the ripple heap
  listed under the game side, where it lives in `rtxvulkan/trace/ripplepass.cpp`. Fix both.
- [ ] **measure** `rtxrenderer.hpp:353` `sWorldViewsPerFrame = 3` on a crossing frame; `renderer.cpp:179-185` the
  world's update traversal under a modal box. `--frame-times` on crossings and a nested frame.

## Batch 15 — The harness

Files: `apps/rtxtool/**`, `tools/omw/*.py`, `files/rtx/views.cfg`, `apps/components_tests/rtxtool/**`.

The tour stands on three items here, which is one reason its branch is cut from this one's tip (Branches):
`Verb` and `VerbSet`, the one `ExitStatus`, and a `plan<Verb>` a verb. They lead the batch, the types ahead of
the plans that name them. The stop a content lacks is passed over since `3102293fd6`.

- [ ] `verbs.hpp:16-33` — `Verbs` is one command and a set of them; `policyOf` of a set dies at run time. `Verb`,
  `VerbSet`, one `constexpr` table held complete by a `static_assert`.
- [ ] `main.cpp:747-768`, `compare.cpp:330-345`, `tools/omw/noise.py:24` — exit 1 means failed, noisier, moved and
  unknown verb alike. One `ExitStatus`, mirrored once in Python.
- [ ] `main.cpp:127-287`, `hosted.cpp:75-96`, `model/runrecord.cpp:19-26` — `RunSetup` in four structs, edited in
  one after being copied to another. `sessionFor` consumes `Framed` into the request; the header reads from it.
- [ ] `model/maprules.hpp` — an enum of one value carried as an `optional` through four structs, always "not
  `view`". Delete; ask `!mPlayed`.
- [ ] `main.cpp:464-833` — `scene`, `shot`, `bench`, `check`, `view` plan their stops in the game-linked
  executable, where no test reaches; `noise` and `film` plan in the library. A `plan<Verb>` each.
  `planCheck` carries the verb's own frame beside its hold (Batch 0), where a test then reaches both.
- [ ] `stopwriter.cpp:455-675` — sixteen checks judged in a static switch of the picture writer that uses none of
  its state; `reportScene` reads only the scene. `checks.cpp`, and `describeScene` in `instruments/`.
- [ ] `model/benchrun.hpp:509,514` — pictures and extents return through raw out-pointers stored in the request.
  `runHosted` returns `SessionResult`, which owns them.
- [ ] `measurer.cpp:267-306`, `stopwriter.cpp:90-104`, `homekey.cpp:86-118` — three routes write a PNG, two of
  them encoding on the frame thread (60 ms a 1080p frame by the tree's figure). One `PictureWriter` with a
  bounded queue.
- [ ] `model/pictureshelf.hpp:17-19`, `compare.cpp:303-316` — `shot --against` keeps every picture at 16 bits
  until the run ends (0.6 GB at 1080p for 37 views). Compare at write.
- [ ] `model/benchrecord.cpp:31-238` — some sixty fields formatted raw where `jsonNumber` writes `null` for a
  non-finite value. Every number through it.
- [ ] `instruments/cardwatch.cpp:144-181` branches on the vendor three times after choosing it once (one
  `CardReader`); `instruments/frametimes.cpp:211-232` zone rows searched by `find_if` over a closed enum (an
  array); `instruments/frametimes.hpp:20-55` perf's fifo protocol in the statistics file; `stager.hpp` a
  stateless class held as a member; the key-edge idiom three times; `runfolder.cpp:27-32` a function-local static
  seeded from the clock; `model/viewfile.hpp:55`, `toolline.hpp:119` say an empty list means every view, and the
  code throws.
- [ ] `options.cpp:505` — the `--warmup` help text describes one machine's GPU clock. A role, not a machine.
- [ ] Tests: `loadViews`, `loadSuites` and `FrameHashes::read` take a path only, so some forty cases write, read
  and remove a temp file (a text core, as `readBlocks`); 26 refusals asserted by exception type alone (a table of
  input and exact words); `refusalOf` written seven times beside an unused `EXPECT_ERROR`.
- [ ] **measure** a timing A/B verb over `bench --json`, interleaved legs, medians and p99 against the leg-to-leg
  spread; `instruments/frametimes.cpp:268-290` two decimals hide a zone under 5 µs.

## Batch 16 — The diff against upstream

Files: upstream's, and `.notes/UPSTREAM.md`. Every change here is a **propose**.

- [ ] Changes with no entry in `.notes/UPSTREAM.md`. **Decided: these six are recorded**, each with its reason
  and what it costs a merge: `mwrender/objectpaging.cpp:69,520-650,1136-1187` (the reference walk handed to the
  ray tracer, `RefKinds`, `PagedCellRef`); `components/myguiplatform/*` and `mwgui/videowidget.cpp:54` (the GUI
  seam); `mwrender/ripplesimulation.cpp:161,236` with `ripplerules.hpp`, `searules.hpp`;
  `components/resource/scenemanager.{hpp,cpp}` (`ModelPrograms::Left`, which reads as a direction and means
  none); `mwlua/postprocessingbindings.cpp:44-56,207-231`; `mwrender/groundcover.cpp:263` (`DensityCalculator`
  in a header of its own).
- [ ] **Decided: these two are reverted in a build under Clang and MSVC**, and whichever the build needs is
  recorded with what it fixes: `components/esm3/readerscache.cpp:35` (the assignment hoisted out of the
  switch's cases); `Q_MOC_INCLUDE` in 16 `apps/opencs` headers.
- [ ] Stale entries: `UPSTREAM.md:174` "five path classes" (two were deleted); `:123` the checks live in
  `cmake/Checks.cmake`; `components/crashcatcher/crash.hpp:34,46` justify `contract` and `notNull` by a compiler
  the tree no longer accepts.
- [ ] `cmake/Checks.cmake` — GCC left on 2026-11-12 and its `-Wdouble-promotion` with it; some 45 upstream files
  differ only by casts written for it, and 34 `Crash::notNull` sites in 25 files were written for its
  flow-sensitive null check. **Decided**: revert each warning-only hunk, build under Clang and MSVC, keep what
  a current check still asks for.
- [ ] `cmake/Checks.cmake:92-93` — `-Wsuggest-override` and `-Wzero-as-null-pointer-constant` force edits in
  OSG-derived files upstream keeps in OSG's style (some 18 files differ by nothing else). **Decided**: the two
  flags off per source for those files, upstream's text restored there, and the checks entry in `UPSTREAM.md`
  names the flags, the files left out and `cmake/Checks.cmake`.
- [ ] `mwrender/pingpongcanvas.cpp:54-65,222-228,396-410` — the rasterizer's gamma resolve is recorded (the SDL3
  port's entry) and runs in no build the game starts, as the rule that keeps the rasterizer unstarted intends.
  It stands; no further rasterizer-only behaviour is written.
- [ ] `mwlua/postprocessingbindings.cpp:207,227`, `mwgui/windowmanagerimp.cpp:2473-2481` — nine null tests of the
  post-processor where every sibling asks `support().declined…`. One question. **Decided 2026-11-13**: the
  seam's question, as `AGENTS.md` asks wherever the game would branch on its renderer.
- [ ] **measure** `mwscript/visibilitygates.cpp:426,459-463` — each watched global looked up by string twice a
  frame. Resolved to its variant at `rewatch`. The watch count is in the "Visibility gates:" log line.
- [ ] `components/sceneutil/optimizer.cpp:2064` — child-order dedup by `std::find` in a growing vector, quadratic
  where upstream's set was not. A set beside the ordered vector. **Decided 2026-11-13**: the set, inside the
  hunk the fork already changed. Refused: a count of the groups first, a run for three lines.
- [ ] `components/misc/strings/conversion.hpp:64-68,107-143` — `toFloatByStream` compiles into no shipped build.
  A `static_assert` and the path deleted. **Decided 2026-11-13**: deleted, in a file the fork has already
  rewritten; a port to a library without it brings the path back.
- [ ] A scan, as `RtxSourceTreeTest`, that lists upstream files differing from the base beyond layout and fails on
  one no entry names, so `UPSTREAM.md` is held to the diff.

## Batch 17 — Conventions held by a tool

Files: `.clang-tidy`, and the sites it names.

- [ ] `.clang-tidy` holds `portability-*`, `clang-analyzer-*`, `modernize-avoid-bind` and naming; the rules
  `AGENTS.md` states are held by prose. Enable the checks that match them.
- [ ] Some 46 classes delete their copies and leave the moves undeclared (`CommandPool`, `Presenter`, `Surface`,
  `FrameRing`, the resolvers, `CellRing`, `Device`, `Pipeline`, `Upscaler`, `myguirtx`'s). All five, held by
  `cppcoreguidelines-special-member-functions` with `AllowMissingMoveFunctionsWhenCopyIsDeleted` left false:
  the option is Google's reading, and the tree's rule is the Core Guidelines'
  (https://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines/special-member-functions.html).
- [ ] Some 33 functional casts (`image/texels.cpp:108-115`, `environment/cloudmesh.cpp:137-162`,
  `renderer/frameimage.cpp:25-30`, `world/atmosphere.cpp:43`, `myguirtx/rendermanager.cpp:260-262`).
  `static_cast`.
- [ ] `common/runs.hpp:145` a `unique_ptr<std::byte[]>(new …)` (`make_unique_for_overwrite`); `throw
  std::runtime_error` in `spirv` (30), `myguirtx` (6), `rtxwindow.cpp:36`, `rtxrenderer.cpp:213` beside
  `common/error.hpp`'s "four failures and no common base".
- [ ] `mirror/meshreader.hpp:102` a `Result<bool, std::string>` (a named three-state enum);
  `environment/skysheet.hpp:23` a `Result` as a parameter (the caller unwraps). The 126 `Result<T, std::string>`
  stand: every caller only logs, so the message is all the type offers, and an enum a reader would be churn for
  no caller that tells cases apart.
- [ ] Hidden clocks in the core: `renderer/sceneuploader.cpp:66-162`, `preprocess/contentpreprocessor.cpp:23-57`,
  `prepare/contentpreparation.hpp:276`. A clock handed in.

# Part 3. The prerelease tour

On its own branch, `rtx-tour`, cut from the refactoring's once that is done (Branches, above).

A verb of the harness that flies the camera over every land cell a content names and through its most
significant interiors, in the running game, and reports every fault it met with the place it met it at: a
shader that stored a NaN, a frame that hitched, a model or a texture refused, a claim of `check` that failed, a
warning or an error in the log, a stall, a crash. Run on demand, once a content: vanilla with both expansions,
a large mod list with Tamriel Rebuilt, the PBR stack.

When it lands, each decision here folds into `docs/architecture.md` and the measurements into `docs/research`,
and this part is deleted.

## What the tree already has for it

The tour is mostly a new arrangement of parts that exist, which is what keeps it small.

- **A camera that flies through many keys**: `CameraTrack`, `CameraPath` and `Cruise`, which a film's take flies.
- **A plan made by a pure function and described before it runs**: `planFilm`, `describePlan`, `film --plan`.
- **Sixteen claims about what the renderer was handed and drew**: `Check`, asked by `check`.
- **A count of every store that was not finite, by the module that wrote it**: `NotFiniteCensus`, and the first
  such frame kept with its texel (`NotFiniteKeep`).
- **What a frame cost and why**: `FrameSamples`, `Crossings`, `Arrivals` and its worst frames, `MemoryReport`.
- **What the content could not give**: `Rtx::Refusals`, the store's `missed` and `dropped`.
- **A place as text a command can stand at**: `describeBlock`, the block Home prints.
- **A sky that crosses from one weather into another, and a clock that runs fast**: `SkyCrossing` and a
  stop's `mTurnThrough`, which `bench --turn-weather` crosses in `sTurnSeconds`, and a film's `--clock`.

What it lacks: a route made from the content, legs that follow one another without a cut, a fault tied to a
place, and the log's warnings counted.

## The tour's decisions

- **A verb of the harness, never a test of the gate.** `./omw release tour` and nothing runs it but a person:
  not `gate`, not CTest, not CI. Rare ran its core tests before every check-in and its long ones periodically
  (Masella, GDC 2019, https://gdcvault.com/play/1026366/Automated-Testing-of-Gameplay-Features); this is the
  long tier. Unreal's performance tests have the same parts: a camera on a sequence, a warm-up before figures
  are taken, a soak a subject
  (https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/AutomatedPerfTesting).
  Refused: a CTest entry, which the gate would run and which needs content a clone lacks; `bench --views=all`,
  which stands still at thirty-seven places and crosses nothing.
- **The route is made from the content that loaded, by a pure function.** `planTour(WorldMap, TourPacing)` in
  `model/`, beside `planFilm` and tested as it is; the executable reads the `WorldMap` once from the game's
  store. So Tamriel Rebuilt, or whatever a profile adds, is covered by loading it. Refused: a keys file a
  content, which a mod's release outdates; a plan made before the load, as a film's is, since the cells are not
  known then; a planning run that writes a file the flying run reads back, two loads and a file read for
  nothing.
- **Exteriors by a boustrophedon sweep, rows `2·CellGridRadius + 1` cells apart.** Back and forth along the
  rows, the canonical cover of an area (Choset and Pignon 1997,
  https://publications.ri.cmu.edu/storage/publications/pub_files/pub1/choset_howie_1997_5/choset_howie_1997_5.pdf),
  so every land cell stands in the active grid once: that is where its references, actors and scripts are made,
  and where the walk, the skinning and the lights meet them. Refused: rows a ring's reach apart, which see the
  statics of every cell and activate a third of them; a space-filling curve, which has no straight run to time a
  crossing on.
- **A leg is a run of land in a row, and the sea between two runs is a cut.** A gap of more than a row's
  spacing is not flown: the large profile's land spans 189 cells across with open sea between its provinces.
  Each leg is one stop with a `CameraTrack`; a leg that continues the last one's flight restages nothing.
- **The height is the land's own.** Each key stands a clearance above the highest ground of the cells under it,
  from the land records' bounds, and `CameraPath` splines between keys; the eye looks ahead and down. Refused: one
  altitude for a world, which `island-crossing`'s note records put frames inside the ground at 3,000 units and
  which no figure fits both an island and a mountain range.
- **Flown through a living world, unsettled, at the streaming suite's speed; coverage is counted.** 12,000
  units a second, what `island-crossing` is measured at, with the ring arriving as it does for a player
  (`benches.cfg`: a settled run puts a bake of 28 ms on the frame path). A planned cell is covered when a frame
  drew the whole world while the player stood in it (`FrameReport::isWhole`); one the flight outran is a
  finding, and `--speed` slows it. Refused: a settled
  flight, whose every hitch would be the harness's own wait; a player's walking pace, hours a content; a camera
  that waits for the world, which is no flight a player makes.
- **Interiors: a share for each content file, and within it the rooms with the most references**, decided
  2026-11-13. A file's visits are its share of sixty by the rooms it adds, never under eight nor over what it
  has: proportional allocation with a minimum a stratum, as a survey keeps every group in its sample
  (https://support.sas.com/documentation/cdl/en/statug/67523/HTML/default/statug_surveyselect_details19.htm).
  Within a file, by references, which is what a room costs and where its lamps and clutter are, at most a few a
  settlement by the prefix before a name's comma ("Vivec, Arena Pit"); `[interiors]` in `benches.cfg` adds what
  a person knows matters. At each, the eye stands where `coc` puts a player and turns once round. So the two
  expansions get eight rooms each under every content that loads them. Refused: the sixty largest of the whole
  content, under which the large list's sixtieth room has 1,072 references and `Morrowind.esm`'s largest 959, so
  the base game and both expansions get none; a fixed total with the minimum inside it, which eight files at
  eight each already pass; every room of the two expansions, 194 and some thirteen minutes; every interior,
  5,980 in the large list; a flight between a room's doors, which needs a path through walls the harness has no
  plan for (a later step, if a turn proves blind).
- **A room's references are counted by the tour's own read of the content**, decided 2026-11-13: the world-map
  reader walks each interior's references through the game's readers at the plan and keeps the count, and the
  plan prints what that took. The cell reader's storage answers for exterior squares alone. Refused: keeping the
  count upstream's start already computes and discards (`ESMStore::countAllCellRefsAndMarkKeys`), nothing read
  twice but a change in a function upstream has marked for rework; a count of distinct records a room, another
  measure behind the same private table.
- **The vanilla tour names both expansions on its own line**, decided 2026-11-13:
  `--content=Tribunal.esm --content=Bloodmoon.esm`, which the configuration appends after the profile's own
  list, so the profile and every figure measured on it stand as they are, and the report's header says what
  loaded. Solstheim is then land the sweep covers, and Mournhold rooms the share above. Refused: the two lines
  in the profile itself, which changes what `bench`, `shot` and `noise` load and what the played game loads; a
  profile of its own, a folder that repeats what the PBR stack's already names.
- **Shown in a window, as fast as the card draws; `--pace=real` holds it to the step**, decided 2026-11-13.
  `bench`'s `--window`, on by default, whose mailbox swapchain paces nothing, and the title says the leg, the
  cell and the findings so far (`StandingNote`). Unpaced, a watcher sees the flight some two and a half times
  over at the desk's frame rate, which is enough to see a black block, a hole or a flicker and is the hardest
  the streaming is driven; paced, the world's second is the wall's, a slow frame is the limiter's and is not
  reported, and a hitch still is. Home marks where the watcher is looking as a finding of its own kind, with
  its block and its picture (`HomeKey`), to go back to with `view`. Refused: real time by default, two to three
  times the wait and a gentler streaming test; a pause, a state to enter and leave where a mark and a `view`
  serve.
- **The sky turns and the clock runs all through the flight**, decided 2026-11-13. A change of weather and of
  hour is itself what a tour tests: an emitter made and freed on an ordinary frame, the lamps a
  `NightDaySwitch` lights at dusk, the eye following the light, the moons' phases. One crossing follows
  another with none standing between, `sTurnSeconds` each as `bench --turn-weather` crosses, through the ten
  weathers in an order that holds every ordered pair of them once: an Euler circuit of the complete digraph on
  the ten, ninety crossings and 360 s of world, which a vanilla tour's 380 s flies whole and the large list's
  three times. The game's clock runs at twenty-four times its own speed, a film's `--clock`, a day in two
  minutes of world: three days over vanilla, nine over the large list. Both are functions of the frames
  flown and nothing else, worked out by `planTour`, so two runs agree, `--plan` prints each leg's first hour
  and crossing, and `--from=<leg>` begins where that leg would have stood. Refused: the next of four fixed
  skies a leg, this part's first decision, which stood under four skies and crossed none; the game's rolled
  weather, which no second run repeats; a seeded draw, which covers the pairs probably where the circuit
  covers them exactly; only the weathers the region under the eye rolls, which is what a player meets there
  and leaves every pair a content's regions never roll to the mod that adds one. The cost: a sky no player
  meets at a place, snow over the Ashlands, and a day faster than any played, so a finding names the
  crossing and the hour it stood in and is stood at again under them.
- **Every fault is a finding with a place.** One record: what kind, which leg and frame, the cell, the eye and
  its look, the hour, the sky as it crossed (the weather left, the one gone to, how far), and the detail (a
  module, a texture's name, a log line). The report is the findings, the same kind and detail counted once
  with its first place, each printed as the block `describeBlock` writes, so `view`, `shot` or `bench --cell
  --pos --look --hour --turn-weather` stands there under that crossing; a hitch within a crossing's swap of
  precipitation, or at the hour the lamps light, says so and reads as the sky's. Refused: a pass or
  fail a leg, which says a row of forty cells failed; a log to read afterwards.
- **A hitch is a frame over the budget that took one and a half times the frame before it.** A slow frame is
  over the budget, one frame of the step rate; a hitch is a slow frame that also stands out from its
  neighbour, which is Unreal's definition with the budget in place of its 60 ms (`t.HitchFrameTimeThreshold`
  and `t.HitchVersusNonHitchRatio`, 1.5,
  https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FEnginePerformanceTargets), so a
  slow stretch is one hitch and not a hundred. The two tiers are the ones Android's vitals keep apart
  (https://developer.android.com/topic/performance/vitals/frozen). Each hitch carries what `WorstFrame` holds:
  the meshes that arrived, whether it crossed or rebuilt, and where the time went. Refused: Unreal's 60 ms
  itself, a different claim on every card; a multiple of the leg's median, under which a town's frames are
  hitches of the wilderness's leg.
- **The tour ends by flying its first leg again.** What a soak is read by is whether the figures come back to
  where they began once the load is the same again (https://loadster.com/guides/endurance-testing/): the
  device's heaps, the content's memory and the leg's median, first pass against last. Growth is a cost finding.
  One leg more, some thirty cells, flown under the first leg's hours and crossings again, the turn and the
  clock begun anew, so the two passes differ by the flight between them and not by the sky. Refused: a slope
  over the whole flight, which the content's own density draws.
- **The game's frames, without layers, under the counting shaders: a policy row of its own.** `VerbPolicy`
  ties the three together today: a verb that does not measure gets the counting set, the layers of a debugging
  build and the reference's radiance width, and a tour must trace what a player sees. So which shader set a
  verb runs becomes a field of the row, beside `mMeasures`, and `HarnessShaders` reads that field.
- **A leg that fails is a finding, and the tour goes on.** A stall, a pause nobody closes, a cell that cannot
  be staged: recorded, and the next leg begins. The plan is the content's, so `--from=<leg>` flies the rest
  after a crash, and the run's record, written a leg at a time, says which leg it died in.
- **`--plan` prints what a tour will fly and what it will cost, and flies nothing**, as `film --plan` does:
  the legs, their cells and lengths, the interiors chosen with their counts, the frames, and the minutes at a
  frame time the line names.
- **One profile a process; the driver runs several back to back.** `./omw release tour --config=<profile>` is
  one content. `tools/omw/tour.py` takes several, runs them one after another, never two at once since they
  share the card, and prints one table with the worst status; a Zed task a profile and one for all three, as the
  profiles are the machine's.

- **One pass, under the counting shaders, in the release flavour with no layers**, decided 2026-11-13. The
  census lives only in the counting set, and a figure taken under it is not the build's
  (`VerbPolicy::mMeasures`): NaNs are named by module, hitches are judged relatively, which holds under a
  constant overhead, every millisecond in the report is marked as the census's, and a hitch worth a number is
  measured again with `bench` at its block. Refused: two passes, the shipped set for the times, honest figures
  at twice the time and two flights whose streaming does not line up; the shipped set alone with the frame
  searched for a NaN, which finds one only after a history spread it and names no module.
- **The log's warnings and errors are counted through its listener, chained**, decided 2026-11-13, an upstream
  change to record in `.notes/UPSTREAM.md`: `Debug::setLogListener` holds one listener, which the game's debug
  window takes, so it returns the listener it replaced, as `std::set_terminate` returns the handler, and the
  tour's calls it. Counted in memory by distinct message and by leg, in a bounded table. The cost is a conflict
  with an upstream edit to those lines of `components/debug/debugging.{hpp,cpp}`, a file the fork already
  changes. Refused: the log file read after the run, a file written and read back that counts nothing when the
  process dies.
- **Faults fail the run, costs are reported**, decided 2026-11-13. A NaN, a refusal, a failed check, an error
  in the log, an outrun cell, a stall or a crash sets the exit status; hitches, slow frames, warnings and
  memory are reported and ranked, each with its place, and `--fail-on=<kind>` adds a cost for a run that should
  hold it. Refused: everything fails, under which a large mod list's own warnings fail every tour; nothing
  fails but a crash, under which a NaN has to be noticed by reading.

## The tour's open questions

- **The speed, and the sixty.** 12,000 a second and a share of sixty give the tables below. Half the speed or
  twice the rooms is a line's option; the defaults are what a person waits for, and the first tours set them.
- **How fast the sky and the day turn.** Four seconds a crossing with no weather standing between, and a day
  in two minutes, are a first setting: `--crossing`, `--weather-hold` and `--clock` are a line's options as a
  film's are, a hold doubles the world a circuit takes, and the first tours say whether a weather must stand
  for its own faults to show.
- **A map of the flight.** The report as a picture: a square a cell, coloured by its median frame, marked
  where a finding stands, written beside the report through the PNG writer the harness has. A frame-rate heat
  map from a box run through the whole map is what one studio's tests draw
  (https://realtimevfx.com/t/gdc-2023-vfx-roundtable-discussion-notes/23367, a roundtable note, the studio
  unnamed). Cheap once findings carry their cell; left out of the first version unless asked for.
- **Whether a tour after a shader change waits for its pipelines.** The counting set's cache is its own, and a
  first flight that compiles as it goes reports the compiles as hitches; to be read in how `bench`'s warm-up
  leg and the kernel compile's job meet, before the first tour is believed.

## What a tour is made of

Counted from the three profiles' content files on 2026-11-13: records read by header, a later file's cell
replacing an earlier one's.

| content | land cells | extent in cells | legs | cells flown | interiors | rooms visited |
|---|---|---|---|---|---|---|
| vanilla with both expansions | 1,540 | 52 × 48 | 19 | 556 | 1,328 | 67 |
| the large mod list with Tamriel Rebuilt | 4,725 | 189 × 93 | 56 | 1,689 | 5,980 | 93 |
| the PBR stack | 1,540 | 52 × 48 | 19 | 556 | 1,328 | 67 |

`Morrowind.esm` alone is 1,390 land cells, 16 legs and 1,134 interiors; Bloodmoon adds Solstheim's 150 land
cells and Tribunal none. One of the large list's eleven content files is a script list and holds no cell.

The interiors each file adds, and the visits the rule gives it:

| content file | interiors | most references | visits, vanilla | visits, the large list |
|---|---|---|---|---|
| `Morrowind.esm` | 1,134 | 959 | 51 | 11 |
| `Tribunal.esm` | 96 | 393 | 8 | 8 |
| `Bloodmoon.esm` | 98 | 434 | 8 | 8 |
| Tamriel Rebuilt's mainland | 3,390 | 1,567 | | 34 |
| the Cyrodiil and Skyrim provinces | 504 and 349 | 1,482 and 2,193 | | 8 and 8 |
| the repack's own file | 396 | 2,288 | | 8 |
| the shared data file | 13 | 763 | | 8 |

A cell is 8,192 units, so at 12,000 units a second and sixty steps a second a cell is 41 frames. What follows
is arithmetic, not a measurement: the frame times are assumed from the bench places' medians on the desk
(6.0 ms at `seyda-neen-ship`, `research/2026-11-10-staged-host-writes-cost.md`), the counting shaders are
slower by an amount nobody has measured, and a cut or a room costs a load whose length is the content's.

| content | frames flown | at 7 ms | at 12 ms | its rooms at about 4 s | held to the step |
|---|---|---|---|---|---|
| vanilla with both expansions | 22,800 | 2.7 min | 4.6 min | 4.5 min | 6.3 min |
| the large mod list | 69,200 | 8.1 min | 13.8 min | 6.2 min | 19.2 min |
| the PBR stack | 22,800 | 2.7 min | 4.6 min | 4.5 min | 6.3 min |

So three contents are somewhere between half an hour and three quarters on the desk, before the loads, and
about an hour with every flight held to the step. `--plan` replaces this table with the content's own figures,
and the first run with a measurement.

## The tour's shape

- `model/tour.{hpp,cpp}` — `WorldMap` (land cells with their height bounds, interiors with their counts),
  `TourPacing` (the speed, the clock's rate, the crossing), `TourPlan`, `planTour`, `describePlan`, `stopsFor`.
  No game header; tested with hand-made maps: an empty map, one cell, two islands a sea apart, a row with a
  gap at the limit and one past it; and the circuit of weathers, which holds each of the ninety ordered pairs
  once and ends where it began.
- `model/finding.{hpp,cpp}` — `Finding`, its kinds as an enum, the table that counts one kind and detail once,
  with its bound. Tested for the count, the first place and the overflow.
- `worldmap.{hpp,cpp}` in the executable — the one place that reads the game's store into a `WorldMap`: the
  land cells and their bounds, and each interior with the file that first names it and its references, counted
  by a walk of its own through the game's readers.
- `tourwatch.{hpp,cpp}` in the executable — what a frame's report, the census's and the log's listener hand it,
  turned into findings; the only new thing that runs each frame, and it allocates nothing there.
- `Session` takes a tour's stops where the world first stands, the moment it already reads the game's
  timescale, and a stop whose stand names no cell begins measuring at its first frame.
- `tools/omw/tour.py`, and the tasks in `.zed/tasks.json`.

## The tour's steps

After the refactoring, on a branch cut from its tip. Whoever addresses an item deletes it.

- [ ] `model/tour`: `WorldMap`, `planTour` for exteriors (rows, legs, heights, keys), `describePlan`; tests.
- [ ] `worldmap`: the store read into a `WorldMap`; `tour --plan` prints the plan and its cost.
- [ ] `Session`: stops taken where the world stands; a leg that continues a flight; a failed leg recorded.
- [ ] `model/finding` and `tourwatch`: not-finite stores by module with the kept frame's place; slow frames and
  hitches with `WorstFrame`'s spend; refusals; the checks a leg can answer, asked at its end; outrun cells.
- [ ] The policy row: the shader set a field of its own, the tour's frames at the game's width with no layers.
- [ ] Interiors: the counts and the files in the `WorldMap`, the share a file, the turn on the spot; a test
  that a file of few rooms keeps its eight and one of fewer than eight gives all it has.
- [ ] The window: `--window` and `--pace`, the title's note, Home's mark as a finding.
- [ ] The closing leg, and the first pass against the last as a finding.
- [ ] The turning sky and the running clock: the circuit of weathers and the clock's rate in `TourPacing`, each
  leg's first hour and crossing from the plan, the closing leg under the first leg's; a finding carries its
  crossing and hour. Tests: a leg begins under the same hour and crossing whichever leg the run began from,
  and two plans of one map agree frame for frame.
- [ ] The log's warnings and errors, by the route decided above.
- [ ] The report: findings as blocks, the table a leg, memory at the first leg and the last; the record a leg
  at a time; `--from`.
- [ ] `tools/omw/tour.py`, the tasks (the vanilla one naming both expansions), and a line in `AGENTS.md`'s
  verification list saying when a tour is run.
- [ ] The first tours of the three contents, their figures to `docs/research`, and this file folded away.
