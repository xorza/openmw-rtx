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

Batch 0 has landed, and what it decided is said where it stands:

- **The frame `check` and `repeat` draw, 957x539, and what a smaller and an odd one cost**:
  `apps/rtxtool/framed.hpp` (`sCheckFrame`) and `tools/omw/repeat.py`. The line's own frame is resolved where the
  line is read for its verb (`ToolOptions::read`), where a test reaches it, and not in `commandCheck`.
- **The gate's order, and the release compile building the programs and no test binary**: `tools/omw/gate.py`
  and `cmake/Tests.cmake` (`openmw_add_test`).
- **The layers' shader checks left to `check`**: the GPU tests' `describeValidation`
  (`apps/components_tests/rtxvulkan/support/device/harness.hpp`), over `Rtx::ShaderValidation` and
  `askOfLayer` (`components/rtxvulkan/device/instance.cpp`). It costs less than this plan first said: the
  renderer holds every module's bindings, specialization words and push block against its layout in every
  process (`ShaderCode::stage`), so what the tests gave up is the layers' own `spirv-val` over a specialized
  module and what the module asks of the device.
- **The GPU binary in four shards and the host tests in two**: `cmake/Tests.cmake`. The host tests became the
  step's bound once the GPU shards were faster, 40 s in one process.

Its figures are in
[`docs/research/2026-11-13-gate-step-times.md`](../docs/research/2026-11-13-gate-step-times.md), all of one
machine, a low-end AMD integrated GPU under Windows: without the steps that compile the gate went from
515 s to 301, and one line changed in the renderer's header costs its release compile 98 s
where it cost 224. What is left of the gate's time is the host in a build with every assert, 1537 frames of
`check` and 1800 of `repeat`, the walk 23 ms of a frame at the ship: the renderer's own cost and no part of
this plan.

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
  A suite is no unit to balance by, `RtxVisibilityTest` being 208 of the 499 tests, and no suite of the GPU
  binary spends more than 0.21 s outside its own cases.
- **The prelude beside the build.** Thirteen seconds, and the gate would no longer stop at its cheapest failure.
- **`check` on the release build.** Faster by the asserts it would lose.
- **A shorter crossing, a shorter warm-up, fewer stops.** Each is a claim asked of less: `rooms-held` and
  `crossings-append` are about a long walk.
- **The integration tests in parallel or at another size.** Upstream's script, at 640x480 already, and its tests
  wait on the wall.
- **Frames ahead of a whole world closed untraced.** Under the smaller frame the host's walk bounds them, so
  the trace is not what they cost, and a history a measured frame reads would start elsewhere.

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

- **What every later batch pays for goes first.** Each batch ends in a gate, so the gate's speed is Batch 0.
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

Landed on `master` ahead of the review's branch (Branches). What is left of it:


## Batch 1 — The open decisions, each settled by its run

No code lands here but a spike behind a switch. Each item ends in a decision written into the batch it names,
and its figures in a dated note under `docs/research`. Run on a quiet desktop, one run at a time.


## Batch 2 — The defects

Files: named at each item.

Every **bug** of the review, ahead of the batch its files stand in: the races first, then what no specification
promises, then what is drawn wrong, then what is latent or cannot fail. Pictures move here, by the winding, the
encoder and the clamp, and in no structural item after.

Landed. The pictures and kernels every structural item after it is held to are the desk's
`out/baselines/review/shot-batch2` and `kernels-batch2.txt`; against `master`'s, the batch moved `balmora`,
`ald-ruhn` and `island-crossing-end` by 2 of 255 on at most 0.03% of the pixels, 37 more within the denoiser's
noise, and 531 of 936 kernel tuples, those of the five shaders that use a subgroup operation. A **measure** item
that moves either says so and takes them again.

## Batch 3 — The tests

Files: `apps/components_tests/{rtx,rtxvulkan,myguirtx}/**`, `cmake/Tests.cmake`, `components/testing/*`.

Ahead of the batches whose files these tests hold (Order). The GPU binary's shards are Batch 0's.

Landed but for what the Changed and Declined sections below say.

## Batch 4 — Worker threads and the monitor

Files: `components/misc/monitor.hpp`, `components/contentstore/store.cpp`, `components/rtx/prepare/contentpreparation.{hpp,cpp}`,
`components/rtx/preprocess/preparedimages.{hpp,cpp}`, `components/rtx/common/parallel.hpp`,
`components/rtxvulkan/trace/visibilitypass.cpp`, their tests.

Landed but for what the Changed and Declined sections below say, and the measure item that stands.


## Batch 5 — The content store and the preparation

Files: `components/contentstore/*`, `components/rtx/prepare/*`, `components/rtx/preprocess/**`, `components/rtx/image/*`,
`apps/components_tests/{contentstore,rtx/prepare,rtx/preprocess,rtx/image}`.

Landed but for what the Changed and Declined sections below say.

## Batch 6 — The mirror and the ring

Files: `components/rtx/mirror/**`, `components/rtx/scene/meshtable.*`.

Landed but for what the Changed and Declined sections below say.

## Batch 7 — The scene's tables

Files: `components/rtx/scene/*`, `components/rtx/common/{runs,slots}.*`, `components/rtx/renderer/{renderer.hpp,nullrenderer.cpp}`,
`components/rtxvulkan/scene/scenebuffers.cpp`, `components/rtxvulkan/vulkanrenderer.cpp`.

Landed but for what the Changed and Declined sections below say.

## Batch 8 — The device layer

Files: `components/rtxvulkan/{device,pipeline,spirv}/**`.

Landed but for what the Changed and Declined sections below say.

## Batch 9 — The backend's scene, acceleration and textures

Files: `components/rtxvulkan/{scene,texture,trace}/*`, `components/rtxvulkan/device/requirements.cpp`,
`components/rtxvulkan/shaders/lib/traversal.glsl`.

Landed but for what the Changed and Declined sections below say, and the measure items that stand.


## Batch 10 — Surface transport shaders

Files: `components/rtxvulkan/shaders/trace/{bounce,visibility}.comp`, `shaders/lib/{traversal,shading,lights,peel}.glsl`,
`components/rtxvulkan/device/requirements.cpp`, `components/rtxvulkan/frame/gbuffer.cpp`, `apps/rtxtool/noise.*`.

Landed but for what the Changed and Declined sections below say, and the items that stand.


## Batch 11 — Reconstruction and display

Files: `components/rtxvulkan/shaders/{texture,trace/denoise,upscale,display}/*`, `components/rtxvulkan/{denoise,upscale,display}/*`,
`docs/architecture.md`, `docs/research/2026-11-11-ray-reconstruction-against-super-resolution.md`.

Landed but for what the Changed and Declined sections below say, and the items that stand.


## Batch 12 — Fog, sprites, water and sky

Files: `components/rtxvulkan/shaders/{trace,lib,scene}/*` (fog, sprite, water, ripple, wave), `components/rtxvulkan/trace/*`,
`components/rtx/world/fogbuilder.cpp`, `apps/components_tests/rtx/sourcetree.cpp`.

Landed but for what the Changed and Declined sections below say, and the items that stand.


## Batch 13 — The frame, presentation and the interface

Files: `components/rtxvulkan/{gui,present,display,frame}/*`, `components/rtxvulkan/{vulkanrenderer,framering}.*`,
`components/myguirtx/*`.

Landed but for what the Changed and Declined sections below say, and the items that stand.


## Batch 14 — The game side and the seam

Files: `apps/openmw/mwrender/{objectpaging.cpp,renderer.{hpp,cpp},glrenderer.cpp}`, `apps/openmw/mwrender/rtx/*`,
`apps/openmw/mwinput/rendererkeys.cpp`, `apps/openmw/engine.cpp`, `components/rtx/renderer/frameimage.cpp`.

Landed in part: the items below stand.


## Batch 15 — The harness

Files: `apps/rtxtool/**`, `tools/omw/*.py`, `files/rtx/views.cfg`, `apps/components_tests/rtxtool/**`.

The tour stands on three items here, which is one reason its branch is cut from this one's tip (Branches):
`Verb` and `VerbSet`, the one `ExitStatus`, and a `plan<Verb>` a verb. They lead the batch, the types ahead of
the plans that name them. The stop a content lacks is passed over since `3102293fd6`.


## Batch 16 — The diff against upstream

Files: upstream's, and `.notes/UPSTREAM.md`. Every change here is a **propose**.

## Batch 17 — Conventions held by a tool

Files: `.clang-tidy`, and the sites it names.


## Changed

- **Batch 2, `ObjectStorage::getLight`** — the static lookups stand behind `MWRender::StaticRecords`, which the
  walk and every `ObjectStorage` answer go through, but the view is made at each ask from the environment and
  not handed to `ObjectStorage` at construction: `RenderingManager`, which holds it, is made inside `World`'s
  constructor before the environment reaches the world, and handing the store in changes two of upstream's
  constructors.
- **Batch 2, the accumulator's clamp** — the clamp holds its samples by the accumulator's own `heldSampleShare`
  under the same switch, with no test of its own: what it moves is the push's direction in a frame of noise, and
  the hold is `heldSampleShare`'s, which `RtxAccumulateClampTest` holds.
- **Batch 2, `BottomLevelStore`'s updatable flag** — set where a structure is made, with no test: the scene
  refuses a mesh of no index, and a freed slot's mesh deforms no more, so no mesh reaches the case.
- **Batch 3, `Testing::runProbe`** — four of the six probe scaffolds run through it, the four whose every
  binding is a storage buffer; `halfstore`, `halfmean` and `unormmean` keep their own, as each lays out, clears
  and transitions images the probe writes, which is no part of a shared scaffold.
- **Batch 3, the stale comments** — `harness.hpp`'s figures were read against their tests and stand (32 measured
  frames, seven kernels); the shard counts beside the layers' shader checks are a dated measurement's and
  name the research note.
- **Batch 3, the hosted harness built into the executable alone** — left to Batch 15, whose `plan<Verb>` items
  move the verbs' planning into the library a test reaches.
- **Batch 2, `MaximallyReconvergesKHR`** — AMD's Windows driver 26.8.1 reads a null pointer in `amdvlk64.dll`
  compiling a module that declares both the mode and ray queries (`bounce.comp`, `visibility.comp`; the FSR
  pyramids and `mipchain` compile), found by halving the modules the mode was put on, on the AMD host. So
  `DriverProfile::mReconvergesRayQueries` is false for AMD's own driver alone, whose ray-query modules
  `ShaderCode` hands over without the mode (`withoutRayQueryReconvergence`); its lanes reconverge where its
  compiler puts them, as they did before. NVIDIA's driver and RADV take the mode on every module (`check` on the
  handheld: 59 checks, none failed).
- **Batch 4, the pools** — one `Platform::stopThenJoin` asks every hand of a pool to stop before it joins any,
  where three loops spelled it; the preparation keeps its own mutex and dispatcher, whose waits ask the
  dispatcher again each time they wake, which no queue of `Misc::Monitor`'s holds (a hand is told to wait, to
  let go, to finish, or what to walk, by when it asks).
- **Batch 4, play's chain hands' scratch** — let go of once the queue is drained, as planned, and the let-go
  itself made to let go: `ChainBuild`, `ChainPass`, `ConversionPass` and `PreparedModel` gave their buffers
  back as `buffer = {}`, which a `std::vector` takes as the assignment of an empty initializer list and keeps
  its room, so the preparation's hands never gave back what the dispatcher counted them as giving
  (`Rtx::letGoOf`).
- **Batch 5, the store's tests** — the count cap's path is tested, and a holding store's tables cost some 16 MiB
  at the production caps, so the 47 tests that make one pay that; the drop at a full queue and the drop while
  stopping hang on when the store's thread takes a write, which no test can order.
- **Batch 5, the two cold stores** — two cold vanilla stores now hold the same 19,403 entries byte for byte
  (`docs/research/2026-11-14-content-store-start.md`), with Batch 2's lost write fixed.
- **Batch 5, the store's task** — a variant of its four kinds; a stamp stays a task of its own, one a key and
  `Holding::mStampedMost` at most, which bounds them by count where coalescing them would only shorten the queue;
  `keepResident` counts what it refuses (`Stats::mUnheldInMemory`, in the stop's report); `holds` keeps its
  `ifstream`, as an absent file and an unreadable one both answer "not held", which a `Platform` tail read of two
  files a system would tell apart for no caller.
- **Batch 6, the reader's small costs** — the record maps are hashed (`boost::unordered_flat_map`, as `CellHolds`);
  the rest of that item is declined below.
- **Batch 7, `SlotRows<T>` for the tables that hand-rolled "take or append"** — the null renderer's view
  scenes and GUI textures and the Vulkan renderer's view scenes take and free through `SlotRows`; `GuiTextures`
  keeps its `SlotPool`, as a slot keeps its read-back buffer across its tenants where `SlotRows::take` replaces a
  row whole, which is the reason its doc now gives.
- **Batch 8, `sReferenceAlignment`** — one header holds the maximum (`referencealignment.hpp`), and a test reads
  every access the shipped modules make by address and holds each to it, the greatest equal to it; not a check
  at `ShaderCode::stage`, which would walk every function body the interface's read now skips.
- **Batch 8, the SPIR-V interface's walk** — it stops at the first `OpFunction`; the facts stay keyed and not a
  vector by the bound, which counts every id the bodies define too: 19,537 in a module of 14,088 words.
- **Batch 8, the allocator's block** — its reason is written beside it with the figures it rests on; the
  dedicated threshold's was already there, and no content allocation at the ship crosses it on vanilla or PBR
  (`docs/research/2026-11-14-device-layer-at-a-start.md`).
- **Batch 9, the refit's state** — the store owns the rota (`BottomLevelStore::pickRebuild`) and the size of the
  refit's scratch (`getRefitScratchBytes`), and answers a mesh's scratch by the build's mode; the build infos stay
  in `SceneAcceleration`, which holds the poses and the indices they name.
- **Batch 9, the media's sea and fog** — marked as the tests' reach-ins: the probe stands one shared renderer under
  several seas, which constructor parameters would make again for each.
- **Batch 11, the stand-ins and FSR's reset** — the reset's comment says why it differs from the SDK: the 3.1.4 host
  clears the log luma to one, under `resetAutoExposureAverageSmoothing`, so its pyramid smooths a reset frame from
  it; the two one-texel stand-in images stay each pass's own, as handing one through the denoiser to the composite
  is plumbing for no memory.
- **Batch 12, the eye's view and the emitters' mean alpha** — where the eye's view ends is one function
  (`eyeViewEnds`), which the three sites take with no kernel moving; the emitters' mean alpha stays worked out per
  emitter per frame, at most 64 fetches of a coarsest level an emitter, where a table a texture filled at arrival
  is a table and a pass for that.
- **Batch 13, the small items** — the channels are an array, the interface's vertices are copied by `bit_cast`
  where a resize wrote them as nought first, and a narrow texture widens one loop a format; the composed frame's
  copy into the canvas stays, as the display lays the puffs over it and the digest `check` and `repeat` take holds
  the trace's own answer, not a test's read alone.
  `framestep.hpp`'s order was read against the frame and stands.
- **Batch 14, a rest told as a cut** — as decided, with the stretch closed at a change of worldspace too; no test
  holds that an effect does not outlive a rest, as a `RenderingManager` stands only in a running game, which the
  integration suite drives, and the notice is its two calls (`notifyTimeJumped`).
- **Batch 15, a `plan<Verb>` a verb** — the plans and the staging they share are the library's (`plans.hpp`), and
  `check`'s hold, `shot`'s rules and `bench`'s refusals are tested there; but each takes the line's `Framed`,
  which `frameFrom` still makes in the executable: it derives the ray tracer's settings through
  `MWRender::RtxSettings`, which only the game's library holds and no test binary of the harness links.
- **Batch 15, the harness's tests** — the views, the suites and the frame hashes read from text (`readViews`,
  `readSuites`, `FrameHashes::parse`), and every refusal is held to its exact words in place through one
  `Rtx::Testing::refusalOf`, not gathered into tables; a refusal in the system's own words (a fifo that is not
  there, a file that cannot be opened) keeps its type or its prefix, as those words differ by system.
- **Batch 16, GCC's hunks** — the 50 upstream files that differed by casts alone and the 34 `Crash::notNull`
  sites are upstream's again, with the locals five files had grown around a null check; a hunk of that kind
  inside a file the fork changes for another reason stays, as telling it from the change it sits in takes a
  reading of each of some 440 files.
- **Batch 16, the two checks off per source** — eleven sources, not eighteen: six of the files that differ by
  nothing else are headers, which every includer compiles, so they keep the edits and the record names them.
- **Batch 16, the post-processor's null tests** — the three that asked whether there is a post-processor ask the
  seam; a script's shader keeps its own test of a technique it holds, which upstream's `toString` asks too.
- **Batch 17, all five special members** — the 56 classes of the fork's folders that deleted their copies delete
  their moves beside them, and the 22 that declared no destructor default one; nothing holds the rule yet, as
  nothing in the tree runs clang-tidy (below).
- **Batch 17, the plain `std::runtime_error`s** — the SPIR-V tools' 28 refusals are `Rtx::InputError`s, as the
  module reader's already were; a window SDL could not make is `Rtx::Unsupported`; the renderer's GL check and
  the GUI's six refusals were contracts the fork's own code keeps, so they end as `Crash::contract` and
  `Crash::fatal`. The kernel compiles' "was stopped" stays, an unwinding the compile's own stop catches.
- **Batch 14, the seam's names** — the start and its failure's words are `rtx/rtxstart`'s, and the spec carries
  the player's local data and the game's files, from which the ray tracer finds DLSS; `getUpscalers` still
  answers in `Rtx::UpscalerChoice`, the one list of upscalers the settings window offers and the settings
  store, which a second enum at the seam would only translate.
- **Batch 14, the frame reader** — one part (`FrameReader`) holds the frozen frame and reads into a buffer that
  goes with the read; the frame still lands in that buffer before the image, as `Renderer::readPixels` fills a
  vector every test reads through. The thumbnail's average reads the frame a row at a time.
- **Batch 13, the interface's slot** — advanced on every prepare, drawn or not, by its reading and not by a
  drag's timing: a prepare after one no present showed took the slot it had itself written into, and waited
  on that write. A window's drag is no run the harness takes, and the fix costs a count.
- **Batch 12, `fogfield.glsl` and `puff.glsl`** — the air's field and the puff's light stand apart, every kernel's
  digest the same (936 of 936), and the emitters' pass reaches no traversal; it stays a launch, of one lane an
  emitter, since no count is pushed that a dispatch's last workgroup would stop at, and a few hundred lanes cost
  the same either way.
- **Batch 1 and 12, the lamps' phase** — landed with no switch, as decided: a phase-weighted share beside the
  isotropic sum, in the lamps' volume's free channel, so a puff still reads the sum; each lamp's phase read at
  the middle of its stretch and held to its source's edge. The `air` zone rose 0.02 ms at the median under
  snow and its p99 to 0.55 ms, held at the fog's night view
  (`docs/research/2026-11-14-lamp-phase-falling-shade-lanczos-table.md`).
- **Batch 1 and 12, falling sprites** — out of the self-shadow: `shade` under snow 0.95 to 0.03 ms, the pictures
  under snow and rain moved by 2 of 255 on under half a per cent of pixels (the same note).
- **Batch 11, the Lanczos table** — made by each workgroup from the SDK's reference rather than bound, and read
  with a lerp, so every device reads one table: `upscale` 0.435 to 0.403 ms, p99 1.07 to 0.69 (the same note).
  No capability chooses it: every device this renderer runs on has 32 or 64 lanes.
- **Batch 14, a deforming part of the ring** — settled by reading: nothing keeps a morphed or skinned drawable
  out of a template the ring stands, and the game's visitor poses one in place before it meets it. The carrier
  now hands the visitor the part's source geometry, which the pose never writes; no test stands a morphed
  static, which the ring's fixtures do not build.
- **Batch 9, the top level's run of updates** — measured, and left unbounded: five minutes still among walking
  people, the wait on the device rose 0.23 ms as the boxes widened and still stood under a build every frame
  (`tlas` 0.20 against 0.03 ms); the header records it
  (`docs/research/2026-11-14-top-level-update-against-build.md`). A bound by moved rows would rebuild a busy
  exterior every few frames at 3.45 ms on the handheld.
- **Batch 11, RR's three refusals** — the architecture no longer calls the hit distance with its matrices and the
  colour before transparency refused: it says they are not handed over and not measured, with NVIDIA's guide
  beside them. The A/B under the PBR profile is its own work: the trace writes no specular hit distance a pixel
  today, and NGX takes none without it.
- **Part 3, the tour** — every step stands for `rtx-tour`, which the plan's Branches cut from this branch's tip
  once it is green and on which every tour line and its upstream change go; this run's grant reaches
  `rtx-review` alone, and its harness — `Verb` and `VerbSet`, one `ExitStatus`, a plan a verb — is what the tour
  stands on (Batch 15).
- **Batch 0, the gate's speed on NVIDIA** — measured, and nothing changes: at 957x539 `check` took 22.0 s against
  25.5 at 1920x1080 and a pair 18 s against 24, and four shards of the GPU binary 12.75 s against two's 16.32
  (`docs/research/2026-11-13-gate-step-times.md`). The DLSS renderer's share of a shard was not separated.
- **Batch 15, the timing A/B** — `omw bench --ab=<switch>`, as the noise A/B is: a warm-up leg, the sides in turn,
  each zone's and host row's median of medians and p99 beside the legs' spread, a change within it said so.
  The zone row prints to the microsecond.

## Declined

- **Batch 3, a `SubjectWalk` bundle for the extractor tests' 22 triples** — the scene, the context and the
  extractor are three declarations a reader sees whole, and a bundle renames every use in 22 tests for no
  defect.
- **Batch 3, one death a contract** — each of `scenedesc.cpp`'s eight deaths holds an assert of its own
  stream, which one death would leave untested; the core's 35 cost some 0.35 s of the host binary's 8.
- **Batch 3, no layers in a death test's child** — measured: two of the GPU binary's death tests take 0.33 s
  and 0.66 s with the layers in the child and without them alike, three runs each; the re-exec is the cost.
- **Batch 3, the presenter suite's device in `SetUpTestSuite` with an empty cache** — measured: 0.58, 0.17 and
  0.30 s with the suite's pipeline cache and without it alike; the device is a tenth of a second a test, against
  tests that lose and remake the surface they would share.
- **Batch 3, the chain build's whole-chain digests** — a digest of bc7enc's and rgbcx's whole output is what the
  test promises; one a level names a level and no texel, for every golden written again.
- **Batch 4, a bound in bytes on play's chain queue, and a job superseded** — a job is a file a slot of a
  scene's texture table holds as it asks, one a key, so the queue holds no more than the tables do and pins no
  pixel the scene does not; a job whose file went unheld while it queued costs one chain at a crossing, and no
  test can order the drop before a hand takes it.
- **Batch 4, play's entries keyed by the image's name (`ChainIdentity`)** — a key by name outlives its file, and
  with it a made image no slot collects; the observers on the file are what let that image go, and the sweep
  what drops their entries.
- **Batch 4, the stop reported through `JobProgress` and a wait on the job** — a stop comes only as the pass
  goes, when nothing waits on its job, so the exception it stores is never read; the tuple compile's 100 ms poll
  is a thread asleep while the full tuple compiles for seconds.
- **Batch 1, the content store's layout, and Batch 5's change of the start's sweep** — neither the kind in the
  path nor the trailer read first: the sweep lists 126,370 files in 0.15 s and reads the 86,640 small ones whole
  in 0.26 s warm and 1.48 s cold, 94% of them shapes it drops, on the store's own thread from the game's start,
  under a load that takes longer (`docs/research/2026-11-14-content-store-start.md`). Batch 1's rule: neither
  where the start is under what a loading screen hides.
- **Batch 5, the alpha decoded in the band walk** — the plane is what `ShadingMap`, the normals' build and the cut
  test read beside the colour, through `AlphaImage`'s readers of every format; a second alpha reader a band at a
  time for 17–22 MB a thread, which the preparation's dispatcher already counts in each hand's budget.
- **Batch 5, `Misc::ByteBuffer` for the buffers filled then overwritten** — each fill is a `memset` ahead of a
  write of the same bytes, a 4096 chain's 64 MiB in some 6 ms beside a build of seconds, and
  `describeImage`'s buffer is a parameter every caller and test names.
- **Batch 5, the manifest as flat tables over its read buffer** — measured: its read is 23 to 39 ms of
  11.6 MB and 60,204 entries, once a start, under the loading screen
  (`docs/research/2026-11-14-content-store-start.md`).
- **Batch 6, `mMoved` trimmed of entries with no home** — an entry with no home is a move a cell held later needs,
  and the adoption's pass over it is a binary search a moved reference, under a millisecond at a session's
  thousands.
- **Batch 6, `ChainKeys` by counted holds, and its `retire` measured** — `retire` shows no sample in the
  `streaming` suite's profile on the large profile (`SceneExtractor::retire` 0.74% of the process, its own
  0.13%); OSG's count is exact, and a count of our own beside it is one every holder must keep in step.
- **Batch 6, the beside-walk cached a model, `materialresolver`'s hash, the reader's measure after every cell,
  and `asSwitch`/`jumps` asked twice** — the walk's places are the model's as it stands at each read, which a
  cache by model across reads holds stale where it animates; the rest show no sample of their own at the still
  ship or along the `streaming` suite on the large profile.
- **Batch 6, a lent model's float copy freed after its first adoption** — the arrays are the reader's, lent by
  pointer; freeing them on the frame's thread and reading the model again on the reader's for one released and
  named again is a second hand-over protocol, for 35–84 MiB.
- **Batch 6, one shared run for the ground's grid** — measured: 352 cells of ground stand at the ship on every
  content, each with its own 24,576 indices and 4225 texcoords, 129 KiB, 44 MiB on the host and as much on the
  device. A run shared by meshes is a mesh table whose runs are held by count, a change to the table every mesh
  stands in and to the device's arrivals; proposed as a piece of work of its own.
- **Batch 6, the frozen roots' poll** — measured at the still ship on the large profile: `Face::of` 0.63% and
  `pass` 0.63% of the process's samples, about a tenth of the walk's 11.5%; comparing the parts and composing only
  for walked roots buys part of that.
- **Batch 6, `Stood::mTransform`** — 57,054 placements stand at the ship on vanilla, 162 a cell, 3.5 MiB of
  transforms, against composing one again at every move.
- **Batch 6, the crash note and the lamp's root a frame** — neither shows a sample of its own at the still ship
  or along the `streaming` suite.
- **Batch 7, one inverse a moved slot** — a moving slot's matrix is inverted twice in double, once for its
  record and once for its motion: some 0.1 µs each for the tens to hundreds of slots a frame moves, against
  threading a double inverse from `recordOf` into `moveRecord`, which water's lowered placement still could not use.
- **Batch 7, `InstanceRecord` as scratch for changed slots** — `placeCompacted` writes the whole row of every slot
  placing a compacted mesh (`placeRow`: transform, mask and flags, not the structure's reference alone), and a
  grown row table writes its new rows, each from the kept record.
- **Batch 7, one `apply(const LiveSettings&)`** — the six setters cost six different things: the upscale rebuilds
  every target and may throw, which its caller catches to stay where it was; the vertical sync rebuilds the
  presentation; the anisotropy writes every texture set again; the rest reach the next frame. One call would
  compare every field to find which of those it owes, which the game's pending settings already know.
- **Batch 7, every texture adder answering a hold** — what `add`, `addSpriteLight` and `addGround` answer is held by
  the material or the layer that names it, which the table counts; a `TextureHold` is for a holder outside the
  tables, such as an emitter or the sky, which takes one through `SceneDesc::holdTexture`.
- **Batch 7, one protocol for the five tables** — the spellings follow the tables: a placement is held by nothing
  and stands and is released, the others count holds; a table with runs beside its rows reserves by the room; and
  each table's news are what the backend reads of it.
- **Batch 7, `FrameRequest::mSky` as a reference** — the request is an aggregate built by designated initializers,
  where a reference member is what the conventions refuse after MSVC bound one to another object.
- **Batch 7, the ground masks as bytes** — 8.3 MiB of float weights on vanilla and PBR and 12.2 MiB on the large
  list (`docs/research/2026-11-14-scene-tables-at-three-contents.md`), of which bytes save three quarters on the
  host and on the device, beside 240 to 293 MiB of tables; and the reader's second path reads a weight in whatever
  format the terrain made, which a byte would round.
- **Batch 7, the tangent words in runs of their own** — 7.2 MiB on vanilla, all of it nought, and 11.3 MiB on the
  large list, on the host and on the device; PBR sets a quarter of its words. Runs of their own move every read of
  a tangent, the posed tangents the deformers write among them, behind a run's offset.
- **Batch 7, a segregated-fit run allocator** — the vertex and index runs reach 1117 to 1516 holes during a load,
  but the vertex runs' 5096 to 6702 `allocate` calls take 0.11 to 0.18 ms in all, 0.9 µs at the longest.
- **Batch 7, `sWorldRoom`'s placements and `GpuInstance::mMotion`** — 38,698 to 68,107 placements stand at the ship
  against a room of 262,144, which is what keeps the top level's growth off the frame a large list's cells arrive
  on; the motion's 48 B a row is 3.1 MiB a copy at the most.
- **Batch 8, a pipeline layout owned by the kernel row** — a layout is two driver objects and the bindings' tables,
  made once a pipeline on the compile hands; shared, every pipeline type would hold its layout by reference to an
  owner that outlives the specialization threads, for no frame's cost.
- **Batch 8, the binding table uploaded at adoption** — the first launch is the adoption: the trace kernels are
  made on compile hands that may not touch the pool, and a tuple specialized in the background is first met at
  the launch that picks it.
- **Batch 8, the timer's queries reset from the host alone** — tried: with the per-zone `vkCmdResetQueryPool`
  gone, two zones of the timer test's 64x64 frame read 0 ms, begin and end on one tick, on the desk.
- **Batch 8, the barriers' and the descriptor writes' arrays value-initialised** — a few kilobytes a pass, and the
  zeros make a read past a count a deterministic one, which `repeat`'s frame-for-frame agreement leans on.
- **Batch 8, the world's build flushed at a staged-byte budget** — the one submit is 2, 23 and 104 ms on vanilla,
  the large list and PBR against a canary of 10 s, and the ring's 56, 299 and 1435 MiB are trimmed right after
  (`docs/research/2026-11-14-device-layer-at-a-start.md`); a flush between textures would first record the
  arrival's writes, which `TextureArrival` orders for the whole batch. The PBR start's 1.4 GiB of staging is
  proposed as a piece of work of its own.
- **Batch 8, `SPV_KHR_float_controls2`'s default in place of per-result decorations** — the decorations are 0.40 of
  the 3.39 MiB of SPIR-V the build ships, and the default needs `VK_KHR_shader_float_controls2` on every target
  driver, which the decorations need nothing of.
- **Batch 9, one `SceneGeometry`** — the build already takes the positions at the call and holds the indices and
  poses it builds over; one class moves five members and every caller for no defect, and the skin pass's three
  writers are what a geometry class would hand out the same way.
- **Batch 9, the samplers, the neutral texel and the stand-in in `ScenePasses`** — eight samplers and two small images
  a scene, made once at its load; held by the renderer, each would be handed to every sync, stand and descriptor
  write of each scene, which the tree's rule against holding a sibling asks for.
- **Batch 9, the build scratch and the spread means handed back after a load** — the scratch stands at 2.3 to 3.0 MiB
  after a load on every content, as its runs bound it; the means at 0, 26.7 and 85.3 MiB, PBR's one map larger
  than the room, which the next arrival of such a map would grow again on its frame
  (`docs/research/2026-11-14-device-layer-at-a-start.md`).
- **Batch 9, one route for a hit's corners** — `ALLOW_DATA_ACCESS` costs the bottom levels no byte on the desk's
  driver (133.0 and 232.3 MiB with it and without), and the fetch is what spares a candidate's test the tables.
- **Batch 10, the pane channels left holding nothing where no see-through placement stands** — 3 of the 36 views
  have none (`./omw release scene --views=all`, vanilla); everywhere else the trace's neutral store is what fills
  the pixels the layers' stage does not, and a clear would write the same bytes.
- **Batch 11, the `if` above the wavelet's and the denoiser's shared walk** — the composing level is the last of the
  loop, whose ping-pong and edge-stop state the loop carries; splitting it to put the choice above hands that state
  between three functions where one optional selects the last level's pipeline.
- **Batch 13, a `FrameTargets` owner** — the two lists, what the targets take and what makes them, stand side by
  side in one file; an owner of the four would take the drain, the reserve and the world's rebuild they are
  made around into it, or leave them outside an interface that hides the order they need.
- **Batch 13, `oldSwapchain` and a present-mode switch** — a recreation follows a resize or a mode change, behind a
  drain the change makes anyway; what it would save is the gap of a destroy and a create, at that frequency.
- **Batch 14, the cell notices by reference** — the seam takes the cell as every upstream receiver of the notice
  beside it does (`mPathgrid->addCell(store)`), so the call reads as the lines around it and the upstream diff
  stays one line.
- **Batch 15, one `PictureWriter` with a bounded queue** — the two writers on the frame thread run only where no
  time is read: `bench --pictures` keeps a run's frames to diff them, and `film` renders offline; Home's
  already writes on a thread of its own. A shared writer adds a thread and a queue for a time nothing measures.
- **Batch 15, `shot --against` comparing at write** — what the shelf holds is bounded by the views a line names
  (74 pictures at 1080p, 1.2 GB, for the whole of `views.cfg` with `--map`) and is gone with the run; comparing
  at write threads the reference folder through the request, the record and the writer, and moves each
  reference's decode into the stop it was drawn at, for memory a desk machine has.
- **Batch 16, the visibility gates' globals resolved at `rewatch`** — vanilla stands 23 gating scripts behind 29
  gates (the "Visibility gates:" line), so a frame reads some tens of globals by name, each one lookup in the
  store's map: microseconds a frame, against a variant handle kept beside each watch in step with the store.
- **Batch 16, a scan holding `UPSTREAM.md` to the diff** — 446 upstream files differ from the base beyond
  layout, and the record is by change, not by file: an entry such as the seam spans dozens of files, so holding
  each file to an entry is an index of the record by file, kept by hand beside it.
- **Batch 17, the checks in `.clang-tidy`** — nothing in the tree runs clang-tidy: the pinned LLVM is kernel.org's
  slim build, which ships none, and upstream's file is read by upstream's own CI. Proposed as its own work: a
  pinned `clang-tidy` wheel from PyPI, as clang-format's is, an `omw lint tidy` over the fork's folders, and the
  checks the conventions state (`cppcoreguidelines-special-member-functions` with its moves required,
  `cppcoreguidelines-pro-type-cstyle-cast`, `google-explicit-constructor`, `bugprone-use-after-move`), each
  finding fixed before its check is on.
- **Batch 17, a clock handed in to the core's three timers** — each times the host's own work for a figure a
  report prints and nothing decides by; a clock parameter through the uploader, the preprocessor and the
  preparation buys exact figures in the one test that reads one, which asks that a hit was counted at all.
- **Batch 14, a `FramePacer` the engine owns** — the pacing's callers past the engine are the window manager's two
  nested loops and the loading screen, which hold the renderer already; a pacer of the engine's is a second
  handle through two of upstream's constructors for state that has one owner today.
- **Batch 13, the interface's textures in block formats, resident by recency** — measured by what the content
  ships: vanilla's eleven splash screens come to 44 MiB as RGBA8, the large mod list's 46 to 244 MiB, so a
  session that sees every one holds that much it never drops. Proposed as its own work, with the residency the
  scene's textures have, since MyGUI keeps every texture it named for the session and tells the backend of no
  release.
- **Batch 13, the interface drawn into the swapchain image** — a windowless renderer, which the harness runs, has
  no swapchain image to draw into: the second route this asks for is the one `mShown` is.
- **Batch 12, froxels past every surface skipped** — the whole `air` zone is 0.23 ms at the fog's night view and
  0.27 ms under snow; a per-column farthest depth carried a frame, dilated and dropped on a cut, is a history of
  its own for a share of that.
- **Batch 12, the ray-generation passes as dispatches** — the gain is a launch's overhead, five a frame, against
  the repeatability the source tree's rule holds; nothing measured asks for it.
- **Batch 12, the self-shadow's two barriers a sprite** — with falling sprites unshaded the `shade` zone under snow
  is 0.031 ms, and 0.014 at the fog's night view.
- **Batch 12, the media's, the composite's, the ripples' and the field's reads** — `media` 0.10 ms and `waves` 0.08
  under snow at Seyda Neen, `puffs` 0.066 at the fog's night view, where the visit test is what a sparse frame
  pays; under snow `puffs` is 2.2 ms, the march over every pixel the flakes cover, which a visit test does not
  touch. The field's three fetches in a room are a share of an `air` zone of 0.23 ms.
- **Batch 12, a room's haze by its height** — the height falloff from nought thins a room authored high, but
  how many rooms stand high is a scan of the content's interior heights no tool here makes, and the even
  room the fix would draw is a change to the column's closed form, which divides by the layer's height.
  Proposed with that scan.
- **Batch 14, three world views a frame on a crossing, and the world's update under a modal box** — the island
  crossing's worst frames are the walk, the trace and the placement (111.6 ms: trace 22.7, place 19.9, present
  16.0 on the low-end AMD host's gate), none the map's views; a modal box's update is a frame nobody plays.
- **Batch 9, the skin's pushes, fast trace on rota'd bodies, and the top level's pack** — `skin` is 0.042 ms at the
  fog's night view and 0.032 under snow, and `tlas`, the pack and the update together, 0.03 ms among the Mages
  Guild's walking people: none is a figure a rewrite of its pass would move by what it costs.
- **Batch 10, the exact mode and the three biases it would size** — level-0 cutouts, a lamp estimate a channel,
  no rates and no filter are a variant of every trace stage and a harness switch over them, built to answer
  three questions of bias none of which a picture or a test has yet shown; proposed as its own work, with the
  cutouts' coverage (Enderton et al. 2010; Castaño's alpha mipmaps), the landing's roughness floor (Cycles'
  Filter Glossy; Weier et al. 2021) and the lamps' three channels as its first three answers.
- **Batch 10, spatiotemporal blue noise, the streams and the lamp histogram** — `noise` holds every place's frame
  as clean as its bar on the suite, so no figure says a mask would move it; proposed beside the exact mode.
- **Batch 11, Catmull-Rom history for the specular and pane filters** — `noise --strafe --walk` under the PBR
  profile is the A/B, and nothing measured asks for it: the frames hold their bar. Proposed with the exact mode.
- **Batch 11, a quarter of the pixels in the exposure's histogram** — `exposure` is 0.025 ms at the fog's night
  view and 0.029 under snow.
- **Batch 11, the means as one image, the mask and the temporal pass once, the bloom in R11G11B10F** — `bloom` is
  0.08 ms; what the merged images save is memory no budget has run short of, against the passes' own reuse
  of each field's image.
- **Batch 4, one thread budget split among the three pools** — the compile and the preparation run together only
  on a cold start, behind one loading screen that waits on both (`RtxRenderer::prepareContent`), and both are
  bound by the processor: oversubscribed, the machine is busy until the sum of their work is done either way,
  and a split decides which of the two finishes first, not when the screen lifts. The chains' pool parks.

## Decisions

- **One branch, `rtx-review`, for the review's every batch.** The plan's own branches (Batch 0 on a short branch
  of `master`, the tour on `rtx-tour`) need commits on more than the one branch this run may commit to; Batch 0
  had already landed on `master`, and what is left of it is measured and recorded here. Refused: committing to a
  second branch, which the run's grant does not reach.
- **`Misc::Monitor::give` answers whether its write gave work**, and publishes the work and the worker's waking in
  that one lock hold; `under` stays for what gives a worker nothing. Refused: a token handed to the write
  (`Gift::given()`), one more type for a bool; waking on every `give`, which on the frame's thread wakes the store's
  worker on every shape asked. The condition variable's own rule that a waiter's predicate is changed under the
  mutex it waits with (https://en.cppreference.com/w/cpp/thread/condition_variable).
- **`pinFloatArithmetic` is `pinModule`**, as the pinning now declares the module's reconvergence beside its float
  environment: a name that says less than the function does is a wrong name.
- **`MaximallyReconvergesKHR` on every entry point of a module that declares a subgroup capability**, and on no
  other: the five shaders that move (`bounce`, `visibility`, the two FSR pyramids, `mipchain`) are those that use a
  subgroup operation (`./omw kernels`, 531 of 936 tuples). Required at device creation; the desk's NVIDIA driver,
  RADV on the handheld and AMD's Windows driver 26.8.1 all offer it
  (https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_maximal_reconvergence.html).
- **A source-tree rule is a row that shows it can break**: each line rule holds an example its match must find,
  and every file or name it allows must break it, or the allowance is dead and fails the rule. Refused: a check
  that the rule matched anything at all, which a rule with an allowance always passes. The tree is read once a
  suite (`SourceTree`), lazily, as Clang-Tidy's and Include What You Use's runs read each file once.
- **`CountingRenderer` is a `NullRenderer` that records**, `NullRenderer` no longer `final`: one more pure
  virtual on `Renderer` is then met once, by the backend that keeps every promise, as a gMock fake delegates to
  a real object for what it does not record (https://google.github.io/googletest/gmock_cook_book.html,
  "Delegating Calls to a Fake").
- **The wave model stands in `world/`**, with the sky and the air a frame is told: it reads `image/` and the
  shared headers alone, and the backend reaches no further into the core than `renderer/`.
- **A held entry is counted as well as weighed** (`Holding::mResidentCountMost`), and the table sized to the
  count: by the bytes alone it was sized for an entry of no payload in every byte, 2^24 buckets. 2^20 entries,
  as many as the keys stamped, ten times the files a large list's store holds. RocksDB's and Caffeine's caches
  take a capacity from their owner the same way (`LRUCacheOptions::capacity`, `maximumSize`).
- **A store stamps only what ranks its start's load**, a small entry of a kind not held at any size: nothing
  else reads a file's time, so every other stamp was a `utimensat` for nothing.
- **A stored ask hands its chain over** (`ContentCodec<MadeChain>::encode(MadeChain&&)`): the preparation reads
  no output it stored, so the bytes move into the payload, where a copy held a chain twice.
- **The walk has a version** (`TemplateWalk::sVersion`), which the manifest's signature carries where a bare 1
  stood, held by a test that digests a fixed model's keys, as LLVM's and Bazel's caches key on a tool's version
  beside its inputs.
- **A cell's references are sorted by radius on the reader's thread**, stably, where the frame sorted the
  placements it adopted, a buffer taken from the heap on the adoption frame; a reference's parts share its
  radius, so the placements arrive in the size rule's order, which the adoption asserts.
- **The walk is handed its thread's content at each read** (`TemplateWalk::read`), as `AGENTS.md` asks of a part
  that needs another's state; the hand and the cell reader own a `ThreadContent` and ask it directly, where the
  walk forwarded eight of its methods.
- **The host geometry mirror stands** (Batch 1), and its header says why: 100 to 153 MiB at the ship
  (`docs/research/2026-11-14-scene-tables-at-three-contents.md`), which a target rebuild writes to the device
  whole again, since `setUpscale` and `resize` let the world go. Freed, every rebuild would read each mesh from the
  content again, against the rule that what was read once stays in memory.
- **`SceneDesc::addEmitter` takes an `EmitterSpec`**, the struct `MorphSpec` and `RigSpec` already are for their
  adders: two trailing bools and three defaulted values become fields a call names.
- **The command pool, its staging and the batch are three types in three files** (`device/commands`,
  `device/memory/stagingring`, `device/memory/batch`): the pool hands out command buffers and submits, the ring
  owns the staging memory, and a batch is one recording and what it holds. The pool lends the ring
  (`CommandPool::getStaging`), as the device lends the pool.
- **The device's reports are parts of their own** (`PipelineReport`, `FaultReport`), as its pool, clock and
  graveyard are: what the pipelines say of themselves and what a lost queue says of where it was, each reached
  through the device that made it.
- **The pool and the clock are asserted to the device's thread** (`OwnedBy`), where a command buffer is taken,
  begun, deferred or submitted and where a value is taken or waited for: the compile hands hold the same
  `const Device&`, and a Vulkan pool is externally synchronized (Vulkan 1.3, "Threading Behavior").
- **A change of anisotropy is written into every scene's sets at once**, behind a drain, as a change of the vertical
  sync rebuilds behind one: a set left owing its samplers until its next placement was a picture's scene traced
  unplaced through destroyed samplers. Refused: owing them to the next `sync`, which only a placement pays.
- **The backend pulls what a scene freed, meshes and textures alike** (`DeviceScene::releaseFreed`), by the tables'
  counts of freed slots: one route where the uploader pushed the textures and the scene pulled the meshes.
- **The frame's puffs word says what was drawn and what of it is lit** (`PUFFS_DRAWN`, `PUFFS_LIT`): the trace
  draws every sprite into the puffs' channel, and the air casts its ambient ray only where a puff's light asks
  for it, which an additive emitter's glow does not. Two bits of one word, where a second word would move every
  constant after it in the frame's block.
- **The harness's exit statuses are 0, 1, 2 and 3**: passed, failed, a usage error, and a difference found
  (`RtxTool::ExitStatus`). 2 for a line the tool cannot read, as POSIX's utilities and Python's `argparse`
  answer one (https://docs.python.org/3/library/argparse.html#exiting-methods,
  https://pubs.opengroup.org/onlinepubs/9799919799/utilities/grep.html); 3 kept for a difference, which
  `omw repeat` already read. Refused: `sysexits.h`'s 64 and up, which no tool beside this one reads and
  Windows has no header for.


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

