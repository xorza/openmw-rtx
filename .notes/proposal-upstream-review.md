# Structural redesign: the upstream-diff review

This proposal answers `.notes/review-upstream-diff.md` (163 findings: 10 high, 27 medium, 126 low). It names the structural change behind each group of findings, the order to make them in, and how each step is checked. Group numbers in brackets, for example [G1], refer to the review's headings.

A finding is not listed one by one where a structural change removes it. Every finding is either covered by a step below, fixed as a local edit in the last phase, or listed under "Not recommended".

## Principles

The findings come from six causes. Each design below removes one cause, not one symptom.

1. **An index must come from a stable key.** A row, a history or an order derived from a count that moves (a table's length, a frame counter, a sort rank) goes wrong when the count moves for another reason. [G1]
2. **One fact has one home.** A rule, a constant, a format or an answer is written once, and every reader derives from it. [G2], [G12]
3. **Untrusted values are cleaned where they enter.** A reader of content or of user input clamps and reports. A builder behind it asserts. [G2 cloud blend], [G11]
4. **An owner owns the whole protocol.** A state that one class writes and another class reads, with a third class that decides it, has no owner. The design gives it one. [G8], [G9], [G10]
5. **A measurement must be able to fail.** A harness check that cannot see the thing it checks is worse than no check. [G5]
6. **The rasterizer changes in three named places only.** Anything else that moves its picture or its cost is reverted or moved behind the seam. [G3]

## Phase 1: correctness fixes that need no redesign

Small, local edits for high and medium findings. Each is one change with one test.

| Step | Finding | Change | Test |
|---|---|---|---|
| 1.1 | [G3] underwater fog to the post-processor (high) | `glworld.cpp:333`: send `world.mAir` to `setFogColor` and `setFogRange`. Keep the water fog for the clear colour. Send `world.mWater.mEnabled` to `setIsWaterEnabled` (the `twf` finding). | A unit test of `GlWorld`'s state feed, if one can be built without a GL context. Otherwise a diff read against upstream's `RenderingManager::update`. |
| 1.2 | [G4] heartbeat (high) | Call `Crash::heartbeat()` in `Renderer::awaitFrame`. Every frame, the video loop, the message box and the loading screen included, opens through it (`openFrame`, `openNestedFrame`, `renderLoadingFrame`). Remove the two old call sites. | The crash-catcher test that counts beats, extended to a nested frame. |
| 1.3 | [G6] sprite sort not coherent (high) | Declare the `Order` block in `spriteshade.comp` `coherent`. | `./omw kernels`: only the sprite shade tuples move. `shot`: no picture moves on NVIDIA. |
| 1.4 | [G1] fog history keyed on the ring slot (high) | The fog volume turns its point pair on each trace the chain records, as `TemporalTurns` does, and not by `getRecordingSlot()`. The ripple impulse copies stay per slot: they are host-written copies, which is the correct use. | A GPU test: trace, skip, trace. The second trace reads the first trace's pair. |
| 1.5 | [G8] emitters pending after a walk that threw | `EmitterResolver::begin()` clears `mPending`. `SceneExtractor::walk` calls it where it clears `mGlows`. | An extractor test whose walk throws, then walks again with fewer effects. |
| 1.6 | [G11] fog depth divides by zero | The reader that makes the room light and the weather fog clamps the depth below 2, and reports it once to `Rtx::Refusals`. `fogExtinction` asserts the range. | A test with a fog density of 2 and of 3. |
| 1.7 | [G11] ripple loop bound | `min(step.mCount, RIPPLE_IMPULSES_MOST)`. | `kernels`: the ripple step tuple moves, and no picture moves. |
| 1.8 | [G5] `shot --against` the directory it writes (high) | Refuse when `out` and `against` are the same directory. Run `compareRuns` whatever the session returned, and combine the two statuses. | A harness test of the refusal. A shot pair with a moved picture prints every comparison. |
| 1.9 | [G5] writes that fail silently | `writeJson` and the Home key check their streams and fail the run. `finish` fails a stop with film frames still pending. `repeat.py` decides "not repeatable" on the hash verdict lines alone. | Tests for the two stream checks. |

**Pictures:** 1.1 moves the rasterizer's picture under water, back to upstream's. 1.4 moves only the frame after a skipped frame. Nothing else moves.

## Phase 2: one home for each fact

### 2.1 The untextured material at a fixed row [G1]

**Now:** the backend puts the sentinel material one past the real materials, and an instance row names it by the table's length. When the table grows, the old index names a real material, and `shade` rewrites the whole table.

**Design:** row 0 of the device material table is the sentinel, for the table's life. A core material index maps to a device row through one function, `materialRow(Index)`, which gives 0 for `sNoIndex` and `index + 1` otherwise. The two writers that put a material row on the device call it: `SceneBuffers::place` (the instance row) and the ground composite's constants (`texture.cpp:862`). The function lives in the first backend folder that both writers may include, by `RtxSourceTreeTest`'s order.

**Effect:** growth appends rows and rewrites none. The full-table rewrite at `scenebuffers.cpp:241`–`:247` goes. No shader changes, because a shader reads the row index it is given.

**Rejected:** making "no material" a real row in the core. It would change the wearing lists, the default traversal facts and every `sNoIndex` test in `SceneDesc` for the same result.

**Test:** a scene with an untextured placement, then a material added. The instance row still names row 0.

### 2.2 The cell grid carries its cell size [G2]

**Design:** a `CellGrid` value holds the cell size, read once from the worldspace (`getCellWorldSize`). `cellOf`, `withinReach`, `distanceSquaredTo`, `chebyshevDistanceTo`, `forEachCellWithin` and `distantLandReach` become its methods. `CellWorld` holds the grid, so the ring, the placer, the reader and the ground reader all read one size. `sCellSize` and `sPreparedBand`'s copy go.

**Test:** the cell-grid tests run at 8192 and at 4096. The ring asks for the cell the eye stands in at both sizes.

### 2.3 The sky rules live in `components/sky` [G2]

`components/sky` already holds `starRoll`, `sunDiscPosition` and `MoonState`. These move there too, and both renderers call them:

- the cloud scroll, with the `Weather_Timescale_Clouds` flag as a parameter (the rasterizer's rule, which the ray tracer's copy dropped);
- the sun's orbit constants (swing 400, northing 75);
- the two per-vertex rules for the cloud shell and the stars (`ModVertexAlphaVisitor::Clouds` and `::Stars`);
- the weather names, built on `ESM::Weather`, so the harness takes "rain" as the game does. The `WEATHER_*` ids leave the shared shader header.

**Test:** the cloud scroll at timescale 30 with and without the flag gives the rasterizer's value.

### 2.4 Device facts that host and shader both state [G2], [G12]

- **FSR formats:** each FSR image's format is a `STORAGE_*` define in `fsr.h`, used by `fsrcallbacks.glsl` and by `upscaler.cpp`. The mip bindings derive from `FSR_PYRAMID_MIPS`. The declarations use `SET_PASS`.
- **Bindings:** `LINE_BIND_SURFACE` and `GUI_BIND_TEXTURE`.
- **The target format:** one constant where the tone pass lives. The four constructor parameters go.

Check: `kernels` shows no tuple moved, because the SPIR-V is the same.

### 2.5 One answer per question in the core and the game [G2]

- `TextureRow::mEncoding` is the one source: `addBaked` takes the encoding, and `describeKept` reads it.
- The cloud blend is cleaned in `SkyReader`, and `describeClouds` asserts.
- The moons' tint and ratio are measured from the loaded faces, as the cloud decks are. The constants stay only for a face that fails to open.
- `GpuInstance` takes its material and opacity from `InstanceRecord`, as it takes everything else.
- `TextureTable::getFreedCount` reads `SlotRows`.
- The stated step has one route into the renderer.
- The visibility gates: one function answers "is this type stood in the distance". A script owns a contiguous gate range. `VisibilityInput` has one identity.
- The normal map's texture unit: one helper in `automaps`.
- `MapEntry`: one "asked" flag for the fog and the map.

## Phase 3: one owner for each protocol

### 3.1 The frame thread owns the content preprocessor [G8]

**Now:** each `SceneExtractor` owns a `ContentPreprocessor` and a `MeanTexels`. A traced view's subject builds its own, and nobody takes its stats.

**Design:** `RtxRenderer` owns one `ContentPreprocessor` and one `MeanTexels`, the frame thread's. `SceneExtractor`, `SkyReader` and each subject take them by reference. `WorldMirror` takes the stats once a frame, from the one owner, so a subject's preprocessing reaches the `preprocess` row. The three comments that say "one a thread" become true.

**The subject's clock:** each draw of a subject sets the world's simulation time on its extractor, so an enchanted glow on the doll moves, as it does under the rasterizer's preview.

### 3.2 The walk's contract is private [G8]

`MirrorTraversal` becomes a nested class of `SceneExtractor`. `addDrawable`, `animate`, `addLight`, `openGlow`, `closeGlow` and `classOf` become private. The eye, the stamp depth and the masks go to the traversal once, in `begin`. The "inside an effect" state moves to one side: the traversal owns `mGlow` beside `mClass`. `Transform` nodes are classified once and the kind is passed down [G13].

### 3.3 The cell ring reaches one scene by one route [G8]

`SceneAdopter` exposes the scene it adopts into and the walk's stats. `CellRing::collect(SceneAdopter&)` takes both from it, so the `ExtractionStats&` parameter leaves five functions, and the ring cannot stand rows in another scene. `CellPlacer` owns the held cells and the spares, so the `std::span<HeldCell>` parameter leaves its five broadcasts.

### 3.4 The hand-over states its whole contract [G9]

- `Renderer::extendScene` appends and does not place. `SceneUploader` calls `placeScene` after it. `VulkanRenderer::extendScene` loses its inner `placeScene`, and the deferred batch rides the placement's submit as it does now.
- Freed meshes are released once per hand-over, in `DeviceScene`, ahead of both paths.
- `Handing::mAdvance` goes. Every hand-over advances, so a picture's scene no longer keeps every change it ever had in `mMoved`. A picture never reads its motion, because its trace sets `mPastLost`.
- The interface says that each texture description names its slot.

**Test:** a view scene changed 100 times holds an empty `getMoved()` after each hand-over. An arrival frame releases each freed mesh once.

### 3.5 The backend uses the device layer's guarantees [G10]

- **One present target.** `CommandPool::begin` opens every buffer with a full barrier on the one queue, so frame N+1 already waits for the blit of frame N. The spare target, the host wait in `claimTarget`, the swap bookkeeping and the branch in `readPixels` go. **Check:** `./omw check` under synchronization validation, and a bench, which should show the host wait gone.
- **GUI staging from `Batch`.** `Batch` gains a method that reserves a staging run and returns a writable span. `GuiTextures` lends from its `mBatch`, whose staging the timeline stamps. The arena ring, `startFrame` and `sStagingArenas` go, and with them the reuse rule that depended on `GuiDrawer`'s wait order.
- **`FrameRing` owns the read-backs.** It clears the counts, orders them for the host and reads them, and it records and reads the picture and the digest. `VulkanRenderer::mReadsCounts` goes.
- **A bound buffer is named.** `LinePass` and `GuiPass` take `const Buffer&` and call `nameForNext` where they bind it.

### 3.6 The visibility gates only where a ground reads them [G3]

`Ground` answers whether it reads gates, by default no. The engine builds and updates the gates only for a ground that says yes, the ray tracer's. Under OpenGL no script is compiled for the gates, and no frame reads the watched values. The engine asks the ground, and never asks which renderer it has.

## Phase 4: measurements that can fail

- **`noise` draws are independent.** A stop carries a sample offset (`Schedule`), which `getSampleFrame` adds. Draw k of the mean starts at its own offset, so its stretch of the sampler's sequence does not overlap another's. The frame and its draws warm up over the same length. The bar's limit starts past the bar's own frames.
- **One kind of mean.** The frame's mean and the bar's limit are both formed from display bytes, or both from radiance. Recommended: both from the renderer's radiance sum, because the display curve is what the verdict must not average through.
- **A route arrives on the last frame.** The camera flies only for a measured index above 0.
- **The texture legend** is printed from the slots `describe` kept.
- **The header** says it is bench's.
- **The reader memory report** counts a refused model, which no cell holds, apart from the lent ones.
- **Whether a run answers keys** is one answer, `VerbPolicy::mPlayed`. See "Decisions for you".
- **Options:** `--day`, `--seconds`, `--warmup` and a repeated view name are refused where they are read. `--load-savegame` and `--random-seed` are declared for framed verbs only.

**Check:** a `noise` run where each of the 32 draws writes a different picture, and whose mean is closer to the reference than any one draw.

## Phase 5: shader constants and per-frame work

- **The shadow denoiser's reach** is one constant. The apron, the tap count, the radius, the storage stride and `KERNEL_SUM` derive from it. The filter's apron derives from its widest step.
- **`HistoryConstants`** replaces the three identical push-constant records. A `Basis` struct carries the previous eye, and `toEyeBefore` calls the shared ray rule.
- **The eye flag leaves the sprites' channel** for a bit of `CHANNEL_SURFACE` that the surface does not use. Confirm first that the chosen bit is unused on every path, sky and misses included. The wavelet, the shadow filter, the glossy filter, FSR's inputs and the tone pass then drop the puffs binding.
- **Constant questions** move to load or to the frame block: the star sheet's extent, the emitter's texture facts, the unit camera basis, and `mFogColumns` in `fogintegrate`. `FsrFrame::pyramidFor` moves to `resize`. `costAt` gets a slot-to-arrival lookup.
- **Parameters that carry nothing:** one facing constant, one `GatherRule` choice, no duplicate extension, no `normalize` on a unit vector.
- **Random draw order:** the fog scatter draws the sky's pairs before the lamp walk, `gather` drops its three dead draws, and `gather` takes the pixel key.

**Check:** `./omw kernels` before and after, and `shot --views=all --map --upscale=off` before and after. Only the random-draw step may move pictures, and it moves the noise pattern and not the estimate. `./omw noise` confirms that. The picture baselines are taken again after that step.

## Phase 6: the water column's sun test (a trade, to measure)

The shaft march calls `skyPassageThrough` at each step, so an occluder under the water shadows the water in front of it. This costs up to `WATER_SHAFT_STEPS` short rays per pixel, only where a shaft shows. Measure it with `./omw release bench` at an underwater view. If it breaks the frame budget, report the figure before any cheaper form is tried.

## Phase 7: local edits

The rest of the review is fixed where it stands, with no design choice:

- [G13] per-frame work: the four-way "every index below N", `tracedterrain`'s map, the duplicate list justification in `placementtable.hpp`, and the driver's second configure.
- [G15] moves: `SpriteBin` and `SpritePasses` to `trace/`, `DebugLines` to `frame/`, the channel list to its own header, `tangent` into `meshtable.cpp`, the harness's step constants to `apps/rtxtool`, `mirrorPrecipitation` to `mirror/`, `sunDiscOf`, the scene root's owner, and `setCullMask`'s forwarder. Each move checks `RtxSourceTreeTest`.
- [G16] bookkeeping: `TextureTable` and `DeformerTable` derive from `HeldRows`, one world-box function, one texture-pass template, one `--out` helper, one config-path function, the settings window's locals, and the importer stamp.
- [G17] dead code, deleted.
- [G18] comments, corrected. This includes `.claude/skills/shader-review/SKILL.md` and the hardware requirement in the settings file, the launcher tooltip, the `.ts` sources and the two CMake files [G2].
- [G3] the `WeatherManager` copy constructor is deleted.
- [G4] a seam member for a GUI frame that is not drawn, and subject views drawn while `tws` hides the world.

## Not recommended

- **Skipping `SolidReach`'s key while the cache is empty** [G13]. The key exists for the cache, and it costs a hash on the first meeting of a texture only. A mode switch would add a state to remove when the cache arrives. Decide it with the cache.
- **A stable key for the light grid's order** [G1, low]. The sort exists for determinism, so the key must come from every light's source: the walk's path identity, the ring's reference, and the effect's root. That is a change to three producers, to save one grid fill when a carried torch passes another lamp's x. Measure the fill first. Take it only if it shows in a frame's tail.
- **The SDL logging fixes** [G3]. They belong upstream. Sending them there is your action, because this session posts nothing to GitHub. They leave this tree when upstream has them.

## Decisions for you

1. **Keys in a bench window.** Recommended: keys follow `VerbPolicy::mPlayed`, so only `view` answers them, and a measured run cannot be moved by a key press. The alternative keeps keys in every window and records each press in the report.
2. **The doll's clock** (3.1). Recommended: the world's simulation time, as upstream's preview animates. The alternative is a still subject, stated in `offscreentrace.cpp`.
3. **One kind of mean for `noise`** (Phase 4). Recommended: radiance on both sides.

## Order and verification

The phases go in the order above: the correctness fixes first, the structure that later steps build on second, and the local edits last. A later phase does not depend on an earlier one's details, except 3.1 before 3.2 (both change `SceneExtractor`'s constructor) and 2.4 before Phase 5 (both touch the shared shader headers).

For each step, as `AGENTS.md` says:

1. Before a phase that can move a picture, `./omw shot --views=all --map --upscale=off --out=<dir>`, and `./omw kernels > before.txt` before a shader change.
2. Build the targets touched, and run the covering binary with a filter.
3. `./omw format`.
4. After a step that changes what a frame reads, `./omw repeat --pairs=10`.
5. `shot --against=<dir>` and `kernels --against`, read against the step's stated picture effect.
6. `./omw gate` once at the end of each phase.

Nothing is committed until you say so. Each phase is a natural commit.
