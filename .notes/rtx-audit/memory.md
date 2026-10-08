# Audit: device memory and data movement

Read-only audit. Nothing was built or run. The figures come from `measured.json` and from
`build-release/perf/bench.{json,txt}`, which is the seyda-neen-ship release bench at 1920×1080
output and 1280×720 render (`upscale quality`). The rest is arithmetic over the formats in the tree,
with the working shown. A vanilla BSA survey was run as a script over the game's archives (see §3).

MB below means 10^6 bytes. "B/px" means bytes per traced (render) pixel.

## 0. Headline

**The renderer's largest memory cost is not content. It is the per-pixel frame targets, and most of
that is the denoiser's history.**

The bench at seyda-neen-ship reports 1688 MiB live on the video heap. Of that, textures are 398 MiB,
structures 148 MiB reserved and tables 142 MiB (`tableBytes` 149.2 MB). That leaves about
1000 MiB, and the frame targets account for most of it:

| owner (render-pixel grid) | B/px | 1280×720 (bench) | 1920×1080 native | 2560×1440 | 3840×2160 native |
|---|---|---|---|---|---|
| `GBuffer`: 21 channels (`gbuffer.cpp:62-90`, `gbuffer.h:62-71`) | 142 | 131 MB | 294 MB | 523 MB | 1178 MB |
| `DenoiseHistory`: 33 images, 12 of them pairs (`denoisehistory.cpp:76-141`) | **440** | **405 MB** | **912 MB** | 1622 MB | 3650 MB |
| `FogVolume`: 64 slices on an 8× column grid, so one froxel per pixel, ~100 B each | ~100 | ~92 MB | ~207 MB | ~369 MB | ~829 MB |
| FSR targets at render size (`upscaler.cpp:331-352`) | ~40 | ~37 MB | ~83 MB | ~147 MB | ~332 MB |
| **sum** | **~720** | **~665 MB** | **~1.5 GB** | **~2.7 GB** | **~6.0 GB** |

The FSR targets at output size (history ×2, output and locks, about 25 B per output pixel) and the
bloom and present targets come on top of the sum.

How the denoiser's 440 B/px adds up:
- Pairs, two copies each, 304 B/px: Surface 16, Colour 32, Moments 32, Fill 32, Fast 16, sky and lamp
  shadow moments 32 + 32, SpecularMean 32, SpecularFast 16, PaneMean 32, PaneHeld 16, PaneFast 16.
- Singles, 136 B/px: Blended 16, Narrow 8 + 8, FillBlended 16, FillNarrow 8 + 8, FastBlended 8,
  two shadow fields at 24 each, and two fast blends at 8 each.

For comparison, NVIDIA's NRD README lists RELAX_DIFFUSE_SPECULAR at 169 MB for its whole working set
at 1080p, of which 97 MB persists and 72 MB is aliasable, and SIGMA_SHADOW at 32 MB
(https://github.com/NVIDIA-RTX/NRD). This denoiser keeps 912 MB at 1080p native, about 4.5× those
two together.

**This decides content room on the target hardware.** `MemoryAllocator::reserveFrame` reserves
`largestFrameAt` over **every** upscale mode, native included (`vulkanrenderer.cpp:82`). Content
(textures and structures) is refused once it would cut into that reserve:
- At a 3840×2160 output, the reserve plans for about 6 GB of frame targets, even when the player
  runs `performance`.
- An 8 GB RTX 2070/2080/3070 is then left with almost nothing for textures, which come down a level
  at a time and then draw the stand-in.
- Cutting B/px is therefore the change that moves the most memory, and it is the one that matters
  on the cards AGENTS.md targets.

## 1. Findings, ranked

### 1.1 Five history pairs that need only one image (−56 B/px). Straightforward.

- **What.** `Colour`, `Fill`, `Fast`, `SpecularFast` and `PaneFast` are declared `mPair = true`
  (`denoisehistory.cpp:79, 89, 97, 131, 139`). For each of them, the "before" half is read by exactly
  one dispatch, and the "now" half is written by a *later* dispatch, with a barrier between the two:
  - `Colour` and `Fill` are read by `accumulate` (`accumulatepass.cpp:65, 72`). The wavelet's first
    level writes them (`atrouspass.cpp:103-105`), after the `ready` batch at `denoisepasses.cpp:100-118`.
  - `Fast` is read by `accumulate` (`accumulatepass.cpp:74`) and written by `accumulate-clamp`
    (`:107`) after the `blended` batch.
  - `SpecularFast` and `PaneFast` are read by the filter (`specularpass.cpp:46`, `panepass.cpp:47`)
    and written by `HistoryClampPass` (`historyclamppass.cpp:43`) after the `written` batch.
- **Why one image is enough.** A pair is only needed where one dispatch reads one half at
  reprojected texels while it writes the other. The table already applies this rule to the shadow
  history ("one image where … the frame reads it before a later pass writes it again",
  `denoisehistory.cpp:66-70`). These five meet the same condition.
- **What the change needs.**
  - Each of the three barrier batches gains the image as a write-after-read
    (`sComputeRead` → `sComputeWrite`).
  - `DenoiseHistory::discard` must not discard a single fed-back image of a filter that is not
    fresh: today it discards what a filter "writes whole", which would destroy the history before
    it is read.
  - `TemporalTurns` freshness is unchanged.
- **Saving.**
  - 56 B/px: 52 MB at the bench extent, 116 MB at 1080p native, 464 MB at 4K native.
  - The frame reserve shrinks by the same amount.
  - Two fewer full-screen RGBA32F images also means less to discard, but no bandwidth change per
    frame: the same texels are read and written.
- **Risk.** Low. The values are bit-identical, so this is a pure lifetime change.
- **Proof.**
  - `./omw check` under `--validation` and synchronisation validation, to catch the WAR hazard if a
    barrier is missed.
  - `./omw repeat --pairs=10`.
  - `./omw shot --views=all --upscale=off --against=<baseline>`: identical up to the documented
    denoiser ulp.
  - `./omw noise --strafe=150 --walk=150`: unchanged.

### 1.2 The glossy filter's images on content that can have no lobe (−56 B/px on vanilla). Your decision.

- **What.**
  - `runs[Temporal::Specular] = mapped` (`denoisepasses.cpp:60`). On vanilla content `mapped` is
    never true: the code itself says "a vanilla scene has nowhere" a lobe.
  - The images are still made at every resize: `SpecularMean` pair 32, `SpecularFast` pair 16,
    `SpecularFastBlended` 8. That is 56 B/px, or 40 B/px after §1.1.
- **Options.**
  1. Make the images where the run can have a lobe at all, decided at construction from whether the
     content can name a companion map (the VFS and the `[Shaders] auto use object normal/specular
     maps` settings). That fits AGENTS.md's "as early as it can be", and no frame pays for it.
  2. Make them on the arrival of the first mapped placement (`InstanceCounts::mMapped`). Exact, but
     a full-screen allocation and discard lands on an arrival frame.
  3. Keep as is.
- **My pick.** Option 1.
- **Saving.** 52 / 116 / 464 MB at the three extents, on vanilla.
- **Proof.** `check`, plus `shot --against` with maps on and off.

### 1.3 Transient aliasing inside the wavelet (−32 B/px, no picture change). Experiment.

- **What.** `Blended` and `FillBlended` are RGBA32F (16 B) and are dead after the wavelet's first
  level. The narrow pairs (`Narrow`/`NarrowOther` and the fill's, RGBA16F, 8 B each) are first
  written at level 1 (`atrouspass.cpp:96-140`). Each narrow pair fits in the memory of its blend.
- **How.**
  - One allocation per pair, with `vmaCreateAliasingImage2` at offsets 0 and size/2. If the driver's
    requirements do not fit two narrow images, size the allocation to the larger need.
  - The first write of each narrow image transitions it from `UNDEFINED`. Its source scope is level
    0's sampled read of the blend.
  - This is the field's frame-graph transient pool: Frostbite's FrameGraph, and NRD's "aliasable"
    pool.
- **Sources.**
  - https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
  - https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in
  - https://github.com/NVIDIA-RTX/NRD
- **Further.** The same table could carry a lifetime column (`Role::Scratch` already names the
  candidates). FastBlended, the shadow scratch and the fast blends could then alias FSR's own
  frame-local targets (intermediate, dilated depth, motion and masks, about 18 B/px), which only live
  inside the upscale zone.
- **Saving.** 30 / 66 / 265 MB at the three extents.
- **Risk.** Medium, because aliasing bugs are hazards. The synchronisation validation layer and
  `repeat` are the proof. `shot --against` should be identical.

### 1.4 The picture chain keeps a full temporal history it never reads (−152 B/px, or −96 after §1.1, of the picture extent). Straightforward.

- **What.**
  - `PictureTracer` owns a `TraceChain` as `MemoryUse::Essential` (`picturetracer.cpp:33`). It grows
    to the largest picture asked and keeps it for the session.
  - Every picture is traced with `mPastLost = true` (`picturetracer.cpp:84-91`), so every filter is
    fresh and no "before" half is ever read.
  - `TraceChain::resize` still makes the whole `DenoiseHistory` (`tracechain.cpp:67`).
- **Size.** The inventory doll is up to 512×1024 (`characterpreview.cpp:122`, 524,288 px). That is
  142 + 440 B/px = 305 MB of essential memory once the inventory has been opened. A 512² map tile is
  152 MB.
- **Fix.** Give `DenoiseHistory` a single-frame mode in which `before()` aliases `now()` and pairs
  make one image.
- **Saving.** 80 MB (doll) or 40 MB (512² tile); 50 / 25 MB after §1.1.
- **Risk.** Low. `before` is never read when fresh, and an assert can hold that.
- **Proof.** `shot` of the map and doll views (`files/rtx/views.cfg`), and `check`.

### 1.5 The frame reserve plans for native whatever mode runs. Your decision.

- **What.** `largestFrameAt` takes the maximum over `sUpscaleNames.values()`
  (`vulkanrenderer.cpp:73-91`). At a 4K output the reserve is the native mode's ~6 GB, while
  `quality` allocates ~2.7 GB.
- **Options.**
  1. Reserve for the current mode only. A mode change that needs more evicts or reduces content in
     the same drain it already does (`createTargets` drains), so the cost is paid at the switch, as
     a world change pays.
  2. Reserve for the modes the settings page can select at this output.
  3. Keep, and rely on §1.1–1.3 to shrink it.
- **My pick.** Option 1. On an 8 GB card at 1440p or 4K output it is the difference between full
  textures and stand-ins.
- **Proof.** A run with `--memory-budget=8G` at a 2560×1440 and a 3840×2160 output, reading
  `reducedTextureCount` and the refusals.

### 1.6 Ground composites are most of texture memory: RGBA8 512² per distant cell. Experiment.

- **What.**
  - Every cell outside the active grid with more than one layer gets a `GROUND_COMPOSITE_EXTENT`
    (512²) RGBA8 image with a full chain (`cellplacer.cpp:247`, `ground.h:27`, `texturecost.cpp:91`).
    Each is 512²×4×4/3 = 1.40 MB.
  - Composites take no part in the side reduction (`chooseSide`).
- **Bound from the vanilla BSAs.** I parsed all three archives' texture headers:
  - Morrowind's *entire* texture set is 142.8 MB on disk: DXT1 75.6, DXT3 55.3, 32-bit RGB 11.9.
  - Completing every one of the 246 chainless files to RGBA8 would add at most 25.8 MB.
  - Seyda-neen-ship's 398.2 MiB (417.6 MB) therefore holds **at least ~245 MB of composites (≥59%)**,
    and likely ~300 MB: about 200–215 of them.
  - Vanilla's file textures are already BC1/BC3 and uploaded as they are. Recompressing to BC7
    would double BC1's size and move the picture, so there is nothing to gain there.
- **Options.**
  1. **Measure first.** Split `TexturesHeld` by `TextureSource` (file, completed chain, bake,
     composite, gloss) in the report. It is a few lines, and it turns the bound above into a number.
  2. **BC1-encode the composite on the device after the bake.** The alpha is always 1
     (`groundcomposite.comp:88`). It saves 7/8, at least ~215 MB here.
     - Runtime GPU block compression of terrain and virtual-texture tiles is shipped practice:
       van Waveren's real-time DXT (id, 2006); Far Cry 4's adaptive virtual textures
       (https://gdcvault.com/play/1021761/Adaptive-Virtual-Texture-Rendering-in); GPU encoders such
       as https://github.com/darksylinc/betsy.
     - It **moves the distant ground**. BC1 error on smooth composites is a few levels of 255, and
       the source layers are themselves DXT1.
     - It needs `shot --against` at every exterior place, and `noise` bias against the reference.
     - It is your call, since the vanilla picture changes.
  3. **Size by distance**, as the rasterizer's quad tree gives a fixed 512² to ever larger chunks:
     512² for the first ring outside the active grid, 256² from about 3 cells, 128² from about 6.
     - Exact where the cone never reads the dropped level.
     - Under anisotropic footprints (`sampleDiffuse`'s `textureGrad`, `texturing.glsl:318`) at
       grazing it does read it, so the picture moves there too.
     - A re-bake on tier change rides `CompositeQueue` (`sCompositesPerFrame = 2`), so no frame
       batches it.
- **Saving.** Option 2: ≥215 MB at this place. Option 3: roughly 60–70% of the composites here, on
  area weighting.

### 1.7 Every static vertex's normal and tangent is kept once per frame in flight (≈ −25 MiB here, and half the load staging for them). Straightforward-ish.

- **What.**
  - `mNormalTable` and `mTangentTable` are `SlotBlocks` (`scenebuffers.hpp:188, 192`). Every mesh's
    normals and tangents are staged into *both* copies at arrival (`scenebuffers.cpp:191-195`).
  - Only skinned bodies change them (`SkinPass`). The positions already use the right design:
    `mPoses` is per slot and indexed by `MeshRange::mBindOffset`, covering the deforming meshes only
    (`sceneacceleration.hpp:186-195`).
- **Fix.**
  - Keep one static copy of the normals and tangents, like the texture coordinates.
  - Keep a per-slot posed normal/tangent table indexed by `mBindOffset`.
  - The shader picks the address and offset by `mesh.mBindOffset != NO_RUN` as a select, not a
    branch, so the one-path rule holds. `GpuMesh` already carries `mBindOffset`.
- **Saving.**
  - At 1.89 M vertices, 8 blocks: one copy of normals plus tangents is 8×(3+1) MiB = 32 MiB.
  - Less one block per table per slot for the posed tables: net ≈ 24 MiB.
  - Load staging drops by 16 B per arriving vertex.
- **Risk.** Low, since the data is bit-identical.
- **Proof.** `shot --against` (identical), `repeat`, and a skinned-body place.

### 1.8 Tangent words are stored for every vertex although vanilla has none (−8 MiB per copy). Straightforward.

- **What.** `MeshTable::mTangents` holds a word per vertex, "nought where the mesh brought none"
  (`meshtable.hpp:131-132`). It is uploaded per slot. `MESH_TANGENTS` already says per mesh whether
  any exist.
- **Fix.** Give the tangents runs of their own, as `mSecondTexCoords` already has
  (`meshtable.hpp:121-125`), with `NO_RUN` for none.
- **Saving.**
  - Vanilla: 8 MiB per device copy down to one block, so 14 MiB with today's two copies.
  - 7.5 MB of host RAM.
  - Less staging at load.
- **Risk and proof.** Low risk. `shot --against` with maps on (tangents read) and off.

### 1.9 The instance table grows on the frame path; the TLAS rows beside it are reserved. Straightforward.

- **What.**
  - `mRowTable.reserve(placementRoom)` (`sceneacceleration.cpp:49`) reserves room for the TLAS rows,
    precisely so "the frame a crossing pushes the rows past" pays nothing.
  - `SceneBuffers::mInstanceTable` (64 B per row, 2 copies) and `mMaterialTable` are never reserved.
    `SlotTable::sync` then `outgrow`s: a new buffer, and a whole-table rewrite into write-combined
    memory.
  - At 80 K rows that is 5 MB per copy, on two consecutive frames, at each doubling. That is a spike
    the posture forbids.
- **Fix.** `mInstanceTable.reserve(placementRoom)` in the `SceneBuffers` constructor, with the room
  the scene is opened with.
- **Second look at the room.** `sWorldPlacementRoom = 1 << 18` (`vulkanrenderer.cpp:69`) is 3.3× the
  suites' largest place (80,324). It sizes:
  - the TLAS storage and scratch for 262 K instances (`sizeTopLevel`);
  - 2 × 16 MiB of host-written rows, and now 2 × 16 MiB more for the instance table.
  
  `1 << 17` (1.6×) halves all of these, and a growth past it still doubles.
- **Saving.** One spike removed. Room: about −16 MiB rows, plus about half the TLAS storage and
  scratch.
- **Proof.** `bench` with a crossing (`--suite` that crosses), reading `crossings.worstMs` and the
  p99. The memory figure comes from the report.

### 1.10 BLAS build scratch and staged positions settle at the first load's high-water mark, for the session. Measure, then decide.

- **What.** `BottomLevelStore::mScratch` and `mArrived` grow to the largest single `build`
  (`bottomlevelstore.cpp:127, 252`) and are kept. The largest build is the world's first: 1457 meshes
  in one batch, with every build's scratch side by side, since NVIDIA asks for "unique scratch memory
  to allow execution without barriers"
  (https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/).
  Later arrivals are a cell's worth.
- **Measure.** Neither buffer is in the report. Add both.
- **Options.**
  1. Shrink both to the steady arrival high-water mark once a load ends. This is an event, not a
     threshold.
  2. Split the load's builds into groups bounded at, say, 64 MiB of scratch, with one barrier per
     group. That is a handful of barriers, at load only.
  3. Keep.
- **Saving.** Likely tens to 100+ MB of essential memory. Unmeasured.

### 1.11 Precision-preserving history formats: the larger lever, against the posture. Your decision.

- **What.**
  - Colour, Fill, Moments, SpecularMean and PaneMean, and both shadow fields' moments, are RGBA32F,
    because a fed-back history must not be stored where a store may round toward nought
    (`fedBackKeepsItsPrecision`).
  - The rounding concern is real: format conversion on an image store may truncate.
  - It can be removed without 32-bit storage: pack halves into `RG32UI` with an explicit
    round-to-nearest conversion in the shader. `ACCUMULATE_FAST` and `HISTORY_CLAMP_FAST` are
    already `RG32UI`.
- **What remains.** fp16 *stagnation*. An update of α·(x − m) below half an ulp is lost: with
  α = 1/32 the mean can stall within about 2^-11·32/2 ≈ 1.6% of its target. NRD runs its whole
  pipeline in FP16 (README) and accepts this; this fork chose not to.
- **Saving.** About 120 B/px all told (111 / 249 / 995 MB).
- **Verdict.** Only `./omw noise` bias against the converged reference can say whether this is
  acceptable. It is listed for completeness, as an experiment, not a recommendation.

### 1.12 Vertex colours as linear float RGB (12 B per vertex). Experiment, census first.

- **What.**
  - `MeshTable::mColours` is `osg::Vec3f`, linear, "white where a mesh brought none"
    (`meshtable.hpp:126-129`), uploaded per vertex (`scenebuffers.hpp:150`).
  - If the authored NIF colours are exact k/255 values, an sRGB byte decoded by the same function
    would be exact and take 4 B. Neither is established.
- **Next step.** A census over the content: are all colours k/255, and does
  `decode(round(255·c))` equal the host's conversion bit for bit?
- **Saving.** 8 B per vertex: 24 → 8 MiB here, plus 15 MB of host RAM.
- **Proof.** The census, then `shot --against`, which must be identical.

### 1.13 Measurement fidelity of the zone timer (harness only). Low priority.

- **What.** `GpuTimer::open` records `vkCmdResetQueryPool` per zone inside the frame
  (`gputimer.cpp:83`), and both timestamps use `ALL_COMMANDS`.
- **Spec position.** The spec scopes a timestamp's dependency to the write alone, and lets
  implementations write it "at any stage that is logically later"
  (https://docs.vulkan.org/spec/latest/chapters/queries.html).
- **Hosts.** The played session has no timer (`mRing(... mCounting || stress ...)`) and checkpoints
  compile out in release (`device.hpp:225`).
- **What to try.** One host `vkResetQueryPool` per frame after `resolve` (`hostQueryReset` is core
  1.2) removes 23 in-buffer resets per measured frame.
- **Check.** One bench A/B with the zones' sum against the frame's wall time shows whether the
  measured overlap of the filters (deliberately unbarriered, `denoisepasses.cpp:100`) is hidden by
  the zone boundaries.

## 2. What is already right (do not change)

- **VMA.**
  - Content pools of 64 MiB blocks, with dedicated memory above half a block.
  - Memory types stated exactly (`memoryTypesFor`).
  - Budget, priority and pageable extensions enabled.
  - Content refused against a ceiling, with stand-ins.
  - Block slack is 99 MiB on 1688 live (5.5%).
  
  This matches VMA's usage patterns and NVIDIA's "use sub-allocation … don't put every resource
  into a dedicated allocation" (https://developer.nvidia.com/blog/vulkan-dos-donts/).
- **Textures.** Vanilla BC1/BC3 files go up as their own blocks, with no re-encode. Shading maps are
  32² R16 (2 KB) and normal spreads R8, both sub-allocated.
- **Barriers.**
  - Batched into one `vkCmdPipelineBarrier2` (`barriers.hpp`), image-scoped.
  - No barrier between the independent shadow, glossy and pane filters (`denoisepasses.cpp:100`).
  - The G-buffer goes from `UNDEFINED` at frame start.
  
  This matches NVIDIA's "group barriers … avoid redundant barriers". The one coarse barrier is the
  head barrier at `commands.cpp:352`. It is measured as costing nothing, and async overlap was tried
  and declined, so it is not proposed.
- **Per-frame uploads are incremental.**
  - Rows are owed and paid per copy (`RowDebt`, `SlotTable`, `SlotBlocks`).
  - Light-grid cells are written by the cell.
  - Only lights, emitters, sprites and presences are written whole, and they are small.
  - The upload zone is 0.28 ms median, 0.43 ms at p99, here (not ~1 ms).
- **Descriptors.** Push descriptors per pass (`dispatch.cpp:22`). One update-after-bind bindless set
  per frame in flight, with a per-set debt.
- **Staging.** A ring of 8 MiB blocks, reused under the timeline (`commands.hpp:298`).
- **Allocation discipline.**
  - The frame path is tested (`framecost.cpp:161-207`, cell ring `cellring.cpp:729`).
  - A texture arrival is bounded at 6 allocations (`framecost.cpp:220`).
  - Gap: no matching test covers a *mesh* arrival (`SceneBuffers::extend`,
    `BottomLevelStore::build`). One modelled on `aTextureArrivingCostsWhatATextureIs` would close
    it.

## 3. Method notes

- The BSA survey script is `scratchpad/audit/scripts/bsatex.py`. It read the DDS headers in
  Morrowind/Tribunal/Bloodmoon.bsa and found:
  - DXT1 ×3866, DXT3 ×895, 32-bit ×90;
  - 246 chainless files (DXT3 64×32 ×43 is the commonest);
  - 25.8 MB as the most completed chains could take.
- Host copies, for the record:
  - `MeshTable` keeps every vertex attribute and index on the host after upload: about 1.89 M × 48 B
    + 9.22 M × 4 B ≈ 128 MB of RAM at this place. The harness digest and a `Rebuilt` read it.
  - Per instance, three host row sets stand side by side: `DeviceScene::mRecords`, `mInstanceTable`
    rows and `mRowTable` rows.
  - Not a device cost, and not proposed.
- Outside this area: 44% of the bench's CPU samples are in a driver thread named `[vkrt] Analysis`,
  in `clock_gettime` and `pthread_rwlock_*` (`perf/cpu-self.txt`). That reads like a spin. Worth an
  `./omw profile --offcpu` and an `nsys` look by whoever owns the host frame.

## Sources

- VMA, resource aliasing: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
- NVIDIA, Vulkan Dos and Don'ts: https://developer.nvidia.com/blog/vulkan-dos-donts/
- NVIDIA, Best Practices for RTX ray tracing: https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/
- NRD README (memory table, persistent vs aliasable pools, FP16 pipeline): https://github.com/NVIDIA-RTX/NRD
- Vulkan spec, queries (`vkCmdWriteTimestamp2`, `vkResetQueryPool`): https://docs.vulkan.org/spec/latest/chapters/queries.html
- Frostbite FrameGraph (transient aliasing): https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in
- Ray cones, Akenine-Möller et al., JCGT 10(1) 2021: https://jcgt.org/published/0010/01/01/
- Runtime block compression: Far Cry 4 AVT, https://gdcvault.com/play/1021761/Adaptive-Virtual-Texture-Rendering-in ; Betsy GPU encoder, https://github.com/darksylinc/betsy
