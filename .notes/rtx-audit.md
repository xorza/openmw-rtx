# RTX renderer audit: memory traffic, allocation and channel count

Five read-only audits of the ray tracer, each against published practice: the G-buffer and the trace's
payload, the denoisers, the ray tracing core and the scene on the device, memory and data movement,
and every other pass (air, water, sky, sprites, upscaler, display). The detail, with every file:line
and every source, is in `.notes/rtx-audit/{gbuffer,denoise,raytracing,memory,passes}.md`. This file
groups the suggestions so that each group can be done as one piece of work.

Nothing here was built or measured. A byte count comes from the formats in the tree. A millisecond
figure is a zone median from the last bench (`seyda-neen-ship`, `quality`, release) or an estimate,
and an estimate says so. Main claims were checked against the code before they went in here.

Sizes: B/px is bytes per traced pixel. 1 B/px is 1.64 MB at 1707×960 (`quality` at 2560×1440 shown) and
3.69 MB at 2560×1440 traced.

## Where the memory and the time are

| Owner | Today | Published practice |
|---|---|---|
| `DenoiseHistory` | **256 B/px** (420 MB at 1707×960) | NRD RELAX diffuse+specular 81 B/px, SIGMA shadow 15 B/px |
| G-buffer: 21 channels | 142 B/px (233 MB); 19 B/px of it written and never read | an NRD game's G-buffer ~40–60 B/px |
| Fog volume: one froxel per 12×12 pixels a slice, 0.44 per traced pixel | ~100 B/froxel (74 MB) | Frostbite and UE ~40 B/froxel; RTX Remix a grid 5× coarser |
| FSR at render size | ~40 B/px | matches the SDK exactly; AMD lists 106 MB at 1440p Quality |
| **Total per traced pixel** | **~485 B/px** | |

- The frame reserve plans for the native mode whatever mode runs (`vulkanrenderer.cpp:73-91`). At a
  4K output that is ~6 GB, which leaves an 8 GB RTX 20/30 card almost no room for textures.
- On the GPU, the frame at `seyda-neen-ship` is 6.8 ms: trace 2.78, the denoisers 1.5 (21–31 % of the
  frame across the places), air 0.41, TLAS 0.41, upscale 0.37.
- Descriptor limits are not a risk: the most a stage binds is 22 storage images, and the widest pushed
  set is 21 against `maxPushDescriptors` = 32 on NVIDIA and RADV.

## The one idea under most of the savings

The full floats in every history exist because this card rounds a half store toward zero, and a
running mean kept in halves fell 1.6 % (bounce), 0.68 % (penumbra), 0.13–0.2 % (lobe). That reason is
true. Full floats are not the only remedy:

- A value a half holds exactly is stored as itself under any rounding mode (Vulkan spec, float
  conversions). The tree already relies on this for the pane albedo (`visibility.rgen:479-485`).
- Rounding to nearest in the shader is **not** enough: an α = 1/32 mean stops anywhere within 16 ulps of
  its target (~0.78 %).
- **Stochastic rounding** is unbiased: round up with a probability equal to the dropped fraction. It
  adds noise of about 0.1 % of the value (~0.04 of a display level). Done in exact float operations
  (`frexp`, `ldexp`, `floor`, `fract`), every vendor agrees, and it is seeded by pixel and frame, so
  `repeat` draws it again. Croci et al. 2022; Connolly, Higham and Mary 2021.

The helper is `lib/halfround.glsl` (`roundedToHalf`), proven by `RtxHalfStoreTest`; the glossy and pane
means use it. The other histories, the fog volume and the payload are group 4.

## Summary of the groups

| # | Group | Saves (1707×960) | Picture | Kind |
|---|---|---|---|---|
| 0 | Measure first: counters and ceilings | — | none | measurement |
| 2 | G-buffer exact packing | 4–8 B/px, 2–3 channels | bit-exact (pane albedo: experiment) | straightforward |
| 4 | Stochastic-rounding halves | history 256 → ~190 B/px; fog −23 MB; payload 30 → 21 words | ~0.1 % noise, no bias | experiment |
| 5 | Transient memory (aliasing) | 50–130 MB | none | infrastructure |
| 6 | Scene tables and structures | ~40–80 MB | bit-exact | straightforward |
| 7 | TLAS and traversal | after measurement | none moved | mixed |
| 8 | Air volume | `column` 0.07 ms, est. −0.02 to −0.04 | ulp-level | experiment |
| 9 | Water | 4 MB; ~19 queue drains a frame | bit-exact / ulp | straightforward |
| 10 | Display and sprites | one sprite table set (up to 64 MB in a storm); one full-frame read | bit-exact | straightforward |
| 11 | Denoiser dispatch shape | est. 0.01–0.15 ms each | bit-exact reachable | experiment |
| 12 | Decisions that are yours | up to GBs of content room | some move the vanilla picture | policy |

Not proposed again: opacity micromaps, async compute, SER (AGENTS.md's declined list), TLAS update or a
split TLAS (against NVIDIA's guidance and the worst-frame rule), R11G11B10F radiance, RGB10A2 normals
(same size, coarser), a visibility buffer, fusing the accumulator with its clamp, a shared-memory tile
in the air's integrate pass (measured 16–29% slower at scale 12: two barriers a slice, against fetches
the texture cache already served).

---

## Group 0: measure first

No picture changes. Each item turns an estimate below into a number, or bounds what a later group can
win. Do this group before groups 6–8.

**Counters the reports do not have** (`devicescene.cpp:194-214`, `readStats`):
- the poses, the TLAS rows (2 × 16 MiB device + host vector) and TLAS scratch for 2^18 instances, the
  refit scratch;
- the BLAS build scratch (`bottomlevelstore.cpp:252`) and the staged arrival positions (`:127`), both
  held at the first load's size for the session;
- the TLAS row count beside `InstanceCounts::mPlaced`;
- the texture total split by source: file, completed chain, bake, ground composite, gloss. The vanilla
  BSAs hold 142.8 MB of textures in all, so at least ~245 MB of `seyda-neen-ship`'s 418 MB is likely
  ground composites (group 12);

**Ceilings, one bench leg each, none shippable:**
- groundcover off (`[Groundcover] enabled = false`): bounds what group 7's grass items can win in
  `trace` and `tlas`;
- the eye's two shadow rays forced to first-hit: bounds group 7's ranged shadow ray;
- `NO_DUPLICATE_ANY_HIT_INVOCATION` off everywhere (`structurebuild.cpp:79`);
- `ALLOW_DATA_ACCESS`'s cost: a `vkGetAccelerationStructureBuildSizesKHR` query with and without it
  over one place's meshes.

**Statistics:**
- the share of pixels whose ambient albedo equals the diffuse one, where the fill adds exactly nought
  (`compose.glsl:29`). If it is near total, most of the fill's whole accumulator path is spent on zero;
- the largest mesh's vertex count per place (group 6's u16 indices), and whether every vertex colour is
  exactly k/255 (group 6).

**Probes and tests:**
- extend `RtxHalfStoreTest`: an overflow row;
- an allocation test for a mesh arrival, beside `aTextureArrivingCostsWhatATextureIs`.

**Outside the renderer:** 44 % of the bench's CPU samples are in an NVIDIA driver thread named
`[vkrt] Analysis`, in `clock_gettime` and rwlock calls. It looks like a spin: `./omw profile --offcpu`
and `nsys`.

## Group 2: G-buffer exact packing

142 → 134 B/px and 21 → 19 channels with no pixel moving; 130 B/px and 18 channels with the experiment.

1. **Each penumbra in its shadow channel's alpha, the open bit as the sign** (−4 B/px, −2 channels).
   `CHANNEL_SHADOWED.a` and `CHANNEL_LAMPED.a` hold one bit each in a half; the radius beside them is
   never negative. Store `±radius` by bits (a closed zero radius is −0.0) and drop both R16F channels.
   Exact: the radius already crosses the payload as a half, and a half store keeps the sign under any
   rounding. One load fewer per pixel per field in `shadowmask.comp`. A small `lib/shadowed.glsl` trio
   keeps the encoding in one place.
2. **Specular albedo as an `R32UI` RGB9E5 word** (−4 B/px). `specularModulation` (`shading.glsl:482`)
   already rounds it to RGB9E5, so the RGBA16F channel spends 8 B on 4 B of data. Needs a uint path in
   the digest (`digest.comp:24`) and `readChannel`; storing the bits through `R32F` is not an option,
   since RGB9E5 words are often float NaN patterns.
   - Experiment on top: the pane albedo in the same `RG32UI` texel (−4 B/px, −1 channel). The trace
     already rounds `paneModulation` before it divides by it, so the unfiltered picture matches; the
     pane filter's input moves, so `noise --ab` at a pane place.
3. Minor: lift is written every frame (4 B/px) and read only under Night-Eye (`tone.comp:138`). Worth a
   launch-variant bit only beside group 11's pane-free variant.

Proof: `shot --against` (nothing moves), `check`, `repeat`; the tests reading `Channel::Penumbra`
(`shadow.cpp`, `light.cpp`) move to the decoder.

## Group 4: stochastic-rounding halves

The largest lever, and an experiment: each history moves to the helper (`roundedToHalf`) in turn, its
tests held to bounds derived from a half's step (`halfRoundedMeanError`). Pictures move by ~0.1 % of
noise and keep no bias; `noise` is the verdict.

**In order of saving:**
1. **The held surfaces to the G-buffer** as last frame's `CHANNEL_SURFACE` and `CHANNEL_PANE_SURFACE`
   (+16 B/px there, −32 here), as RELAX keeps its previous guides; `mDistanceScale` goes away.
   Distances compared at full float.
2. **The bounce and its fill in one texel ("B2")**, for time alone: bounce rgb, fill rgb, the deviation
   and the frame count as eight halves in one `RGBA32UI` texel, where a tap fetches two RGBA16F
   texels today; the first level goes from 93 to 68 fetches a pixel, at the same memory. It costs an
   unpack at every tap and a second read path in the composite beside the unfiltered channels. Only
   if a profile says the fetches limit the levels: measured, the first level is 0.22–0.30 ms and each
   narrow one 0.08–0.13.
3. **Shadow history and scratch**: unorm16 mean + half variance in one word (−16 B/px).
4. **Shadow moments** in the capped EMA form (−32 B/px). A behaviour change: Welford's M2 grows with an
   uncapped count. The SDK's `R11G11B10` moments already stall their count near 128.
5. **Fog history pairs** (`fogvolume.h:30`, RGBA32F → RGBA16F): −23 MB, ~35 MB a frame of traffic.
6. **Payload radiances** (F4 in the G-buffer report): the six full-float radiances (18 of 30 words)
   exist only for summed references, and stochastic rounding is unbiased in a sum. 30 → 21 words on one
   path for both widths. The tree measured up to 0.02 ms per payload word (`a72bc240f4`); the gain may
   be nothing if the hit shader's own registers dominate. Drop it if `bench` shows nothing.
7. **Bloom pyramid in `B10G11R11`** (alpha is a constant 1.0, `bloomdown.comp:49`): 9.8 → 4.9 MB, only
   with the pre-rounded store; ≤ 0.02 ms.

End state: history 256 → ~190 B/px. Proof per item:
`./omw release noise --ab=<switch>` narrowed first (`--views= --strafe=0 --walk=0 --still`), then the
suite; `repeat`; `shot --against` (small differences everywhere, read them); `kernels --against`; then
`bench`.

## Group 5: transient memory

Memory only, no time. Do it after group 4, which shrinks what there is to alias.

- **Real aliasing** (VMA aliasing images, Frostbite FrameGraph, NRD's "aliasable" pool): a lifetime column
  in the image tables and a `constexpr` check that aliased rows do not overlap, the discard moved to
  each alias's first write. Candidates:
  - G-buffer channels dead mid-frame — indirect and fill after the clamp, the pane channels and
    specular after their filters — under the denoiser's frame-only images (52–79 MB);
  - FSR's transients (36.6 MB: the SDK's six aliasable images plus the three shared ones) under the
    denoiser's, which die before FSR starts; the bloom pyramid in FSR's dilated set after it ends.
- Mind: passes that overlap today with no barrier between them (the clamp and the sky mask, the
  specular clamp and the pane filter) would serialise if aliased across.

Proof: synchronisation validation, `repeat --pairs=10`, `shot --against` exact.

## Group 6: scene tables and structures

Bit-exact; one area (`scenebuffers`, `sceneacceleration`, `bottomlevelstore`, `meshtable`).

1. **Static normals and tangents are stored once per frame slot** (`scenebuffers.cpp:189-195`); only
   skinned bodies change them. One static copy, and posed normals and tangents in per-slot blocks
   indexed by `mBindOffset`, as the poses already are; the shader selects without a branch. Est.
   −24 to −40 MB at `seyda-neen-ship` (group 0 counts the vertices), and less load staging.
2. **Tangent words for every vertex** although vanilla has none (`meshtable.hpp:131`): runs of their
   own, as the second texture coordinates have. −14 MiB with today's two copies.
3. **`sWorldRoom.mPlacements = 2^18`** (`sceneroom.hpp`) is 3.3× the suites' largest place:
   `2^17` halves the TLAS storage, scratch and row reservations (≥ 24 MB); the TLAS builds over the
   placed rows alone, so the room bounds only what one place stands.
4. **BLAS build scratch and staged positions**: record a whole-scene build in fixed-size chunks (e.g.
   32 MB of scratch per call, a barrier between), so neither buffer holds the first load's peak for the
   session. Loading then allocates no more than a frame does.
5. **`GpuMaterial` is 108 B** and the any-hit reads fields across up to four 32 B sectors: reorder its
   fields into the first 32 B and pad the row to 128 B.
7. After group 0's census: **u16 indices** (about half the index memory and index bytes per
   candidate); **RGBA8 vertex colours** decoded through a 256-entry table, only if every colour is
   exactly k/255 (~20 MB).
8. `GpuInstance::mMotion` (48 of 64 B) is the identity for nearly every row: a 16 B row and a sparse
   moving table (~6 MB). Low value.

Proof: `shot --against` (nothing moves), `repeat`, `check`, a `bench` leg with a crossing (`crossings`
worst and p99), the new mesh-arrival allocation test.

## Group 7: TLAS and traversal

1. **Groundcover: ~40k cutout instances that every ray type meets.** Upstream's rasterizer never shadows
   grass. Only if group 0's groundcover-off leg says the share is large:
   - merge plants into one BLAS per cell and material (NVIDIA's advice for heavily overlapping
     instances; RTX Remix's `mergeInstancesIntoBlas`), est. +50–150 MB of structures against ~15 MB of
     rows, with a per-geometry identity in the hit;
   - or let some rays skip grass (group 12).
2. **Both of the eye's shadow rays trace to the nearest occluder** for the penumbra. Past the distance
   where the denoiser's reach caps the penumbra, the exact occluder no longer matters: trace nearest up
   to it and first-hit beyond. The bit is identical. Only if group 0's ceiling is worth it.
3. **The cutout candidate is seven dependent loads deep.** After group 6's material reorder, an
   experiment: a per-triangle UV run for cutout meshes (24 B a triangle), three loads instead of seven.
   The opacity-micromap result says any-hit is not dominant, so expect little.
4. `NO_DUPLICATE_ANY_HIT_INVOCATION` only on meshes that can be walked through as see-through, if group
   0's ceiling shows a gain.
5. Merging each actor's parts into one BLAS (one refit per actor); profile the refit zone first.

Proof: `repeat`, `shot --against`, `bench` at the grass places, `noise` for item 2.

## Group 8: the air volume

1. **A segmented scan down each column**: `fogThrough` is affine in the column state, so the front-to-back
   pass is a prefix scan. 4 threads a column of 16 slices lift the occupancy, which at scale 12 has
   some 11,000 columns a frame to run; est. 0.02–0.04 of the 0.07 ms `column` zone. Ulp-level change.
   (The header says one thread a column cannot be improved; the affine form says it can.)
2. Small: `seeing` written only on frames with puffs; `airSunward` and `sliceSunward` merged into one
   RG16F image.

Not recommended: culling froxels behind surfaces (rests on last frame's depth, fails at disocclusions).

Proof: `shot --against` at every fog place with `--map --upscale=off`, `noise --strafe=150 --walk=150`,
`bench`.

## Group 9: water

1. **One SPD dispatch for the wave and wake mip chains** (`Image::buildMips`, `image.cpp:343`): today a
   blit and a barrier per level, ~19 queue drains a frame for 77 tiny blits. Fuse `ripplecompose` into
   SPD's first stage. LEAN's variance is a difference of two means, so the store needs group 4's
   explicit rounding. Est. 0.03–0.06 ms of `waves` + `ripples` (0.245 ms); confirm with `nsys` first.
2. **The wake field stores every height twice** (`ripple.h:42`, RG32F, `.y` is the `.x` the step read):
   a ring of three R32F images. 16.8 → 12.6 MB, step traffic 16 → 12 B a texel, bit-identical.
3. Small: test impulses against each workgroup's rectangle once (worst case only).

Proof: `shot --against` at the sea places (exact for item 2), `repeat`, `kernels`.

## Group 10: display and sprites

1. **The world keeps one sprite bin per frame slot that it does not need.** Every table is device-written
   and the head barrier serialises frames; `TraceChain`'s own comment gives that reason for pictures
   using one bin. Keep one 4-byte report buffer per slot (or the list never grows) and one set of
   tables. Saves up to 64 MB of tile list in a storm.
2. **The histogram re-reads the whole shown frame** (29.5 MB at 2560×1440) right after the bloom's first
   halving read it: count the same pixels in the halving.
3. Sun glare's one-thread easing into the exposure reduce (one dispatch and barrier).
4. Bloom in `B10G11R11` is in group 4.

Already right: FSR matches the SDK (do not deviate), the bloom follows Jimenez with its last tent in
tone, the GUI's two images are required by the design.

Proof: `repeat` and `check` over the storm suite, `shot --against` exact, the exposure tests.

## Group 11: denoiser dispatch shape

Experiments for time, after group 4 settles the formats.

1. **Record the two shadow fields and the filters stage by stage**: every compute barrier drains all
   earlier compute, so interleaving halves the drains. Est. 0.01–0.03 ms. Conflicts with sharing one
   shadow scratch set between the fields; pick one.
2. **The composite fused into the last wavelet level**: −16 B/px of traffic and one dispatch.
3. **A pane-free launch variant**: the four pane channels are 32 B/px (22.5 % of the G-buffer), written
   and filtered every frame even where no surface can be peeled. A conservative "no peelable material"
   fact skips the stores and the pane pass; the channels are cleared once on entry. Up to ~0.15 ms
   (est.).
4. Strided levels at 2560×1440 native no longer fit the 64 MB L2: thread-group ID swizzling, if
   measured.

Proof: `kernels`, `shot --against`, `check` with both `--variants`, `bench`, `nsys` on the filter zone.

## Group 12: decisions that are yours

Each changes a policy or the vanilla picture, so none is a fix to make without your call.

- **The frame reserve** plans for native whatever mode runs. Reserve for the current mode and evict at
  the mode switch, which already drains. On an 8 GB card at a 1440p or 4K output this is the difference
  between full textures and stand-ins.
- **The glossy filter's images exist on content that can never have a lobe** (24 B/px). Make them only
  where the content can name a companion map, decided at construction.
- **Ground composites are most of texture memory** (at least ~245 MB at `seyda-neen-ship`, 512² RGBA8
  each): BC1-encode them on the device after the bake (−7/8), or size them by distance. Both move the
  distant ground.
- **Groundcover met by fewer ray types** (lamp shadow, bounce far hit, fog lamp rays), as upstream never
  shadows grass. Needs an instance-mask bit freed; changes the look.
- **Octahedral vertex normals** (~20 MB): pictures move at the 1e-5 level.
- **The exterior's 145 MB of structures and 150 MB of tables stay resident inside interiors**, for a fast
  return. Keep, keep a band, or release.

## Suggested order

1. Group 0 (numbers), then groups 2 and 6: bit-exact, 4–8 B/px and 40–80 MB.
2. Groups 9 and 10: exact, small, independent.
3. Group 4, item by item behind its helper and test, `noise` as the verdict.
4. Groups 7, 8 and 11, measured.
5. Group 5 once group 4 has shrunk what there is to alias.
6. Group 12 as you decide.

## Sources

- NVIDIA NRD README (memory table, formats, persistent and aliasable pools): https://github.com/NVIDIA-RTX/NRD
- NVIDIA, Best Practices for Using NVIDIA RTX Ray Tracing (Updated): https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/
- NVIDIA, Vulkan Dos and Don'ts: https://developer.nvidia.com/blog/vulkan-dos-donts/
- NVIDIA, Path Tracing Optimization in Indiana Jones (live state): https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/
- AMD RDNA Performance Guide: https://gpuopen.com/learn/rdna-performance-guide/
- AMD, Vulkan barriers explained: https://gpuopen.com/learn/vulkan-barriers-explained/
- AMD FidelityFX SPD: https://gpuopen.com/fidelityfx-spd/ ; FSR 3.1.4 manual: https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/
- VMA, resource aliasing: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
- O'Donnell, FrameGraph (GDC 2017): https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in
- RTX Remix: accel manager https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_accel_manager.h ; volumetrics settings https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.4.0-0/docs/runtimeinterface/renderingtab/remix-runtimeinterface-rendering-volumetrics.html
- Hillaire, Physically Based and Unified Volumetric Rendering in Frostbite (2015): https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite
- Wright, Volumetric Fog and Lighting (SIGGRAPH 2017 Advances): https://advances.realtimerendering.com/s2017/
- Jimenez, Next Generation Post Processing in Call of Duty: Advanced Warfare (2014): https://advances.realtimerendering.com/s2014/
- Schied et al., SVGF (2017): https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced ; A-SVGF (2018): https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/opt-gradient.pdf
- Croci et al., Stochastic rounding (2022): https://doi.org/10.1098/rsos.211631 ; Connolly, Higham, Mary (2021): https://doi.org/10.1137/20M1334796
- Cigolle et al., unit vector representations (JCGT 2014): https://jcgt.org/published/0003/02/01/
- Blelloch, Prefix Sums and Their Applications: https://www.cs.cmu.edu/~guyb/papers/Ble93.pdf
- Vulkan specification (float conversions, fixed-point conversions, queries): https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
