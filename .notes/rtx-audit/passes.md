# Audit: the passes outside the primary trace and the denoisers

Covers the air volume, the sea and the wake, the sky, the sprites, the upscaler and the display chain.
Read-only: nothing was built or run. All sizes are raw texel bytes (no driver padding). Traced extent is
1707x960 and shown extent 2560x1440 (the `quality` upscale) unless a figure says otherwise. Zone times are
medians from `measured.json`.

## Bottom line

- **The air volume is the largest single allocation in this area: 74 MB at scale 12** (165 MB at the
  audit's scale 8, which the figures in §1 are at). The fog volume takes about 100 bytes per froxel.
  Frostbite- and UE-style volumes take about 40, and RTX Remix, a path-traced shipping renderer, runs
  a grid 5x coarser than scale 8. Most of it is the two history pairs kept in RGBA32F, and they are
  32-bit only because this card's half stores round toward zero. If the shader rounds before the store
  (`roundedToHalf`), the store becomes exact and the history fits in halves: 23 MB at scale 12, and
  about a third of the volume's traffic.
- **The `column` zone (0.07 ms at scale 12) runs at low occupancy**, a thread to a column, some
  11,000 a frame, each with a 64-step loop of 18 fetches. A segmented scan, which the transmittance's
  affine form permits, lifts it.
- **The wave and wake mip chains are built with one barrier per level** (`Image::buildMips`): about
  19 queue drains a frame for 77 tiny blits. A single SPD dispatch replaces them. The wake field also
  stores each height twice (RG32F, `.y` is the last frame's `.x`). A three-image R32F ring holds the same
  state in 12 MB instead of 16 MB with bit-identical output.
- **The FSR port matches the SDK exactly**, checked against `ffx_fsr3upscaler.cpp` v1.1.4. Nothing
  there should be changed against the reference. 36.6 MB of its images are transient within a frame:
  the SDK marks six `FFX_RESOURCE_FLAGS_ALIASABLE`, and the three "shared" ones are transient too in an
  upscaler-only integration. They are the natural place to alias the bloom pyramid and the denoiser's
  transients. That needs an aliasing allocator, which the tree does not have.
- **The display chain is already close to published practice.** The bloom's last tent runs inside the
  tone pass, and the glare is folded into tone. What is left is small:
  - the bloom pyramid writes a constant alpha (25% of its bytes);
  - the histogram re-reads the whole shown frame, which the bloom's first halving has just read.
- **The world's two sprite bins look redundant.** Every bin table is device-written and frames are
  serialised by the head barrier. The `TraceChain` comment gives exactly that reason for the pictures
  using one bin.

Nothing here re-proposes the declined items: async compute, opacity micromaps and SER.

## Ranked findings

| # | What | Saving (1707x960 / 2560x1440) | Kind | Risk / proof |
|---|------|------------------------------|------|--------------|
| 1 | Fog history pairs RGBA32F → RGBA16F, with an explicit round-to-nearest (or stochastic rounding) before the store | 52.6 MB; about 79 MB a frame of traffic; ~0.05–0.1 ms of `air`+`column` (estimate) | experiment | Moves the air by under a half ulp. Proof: `noise` suite, `shot --against` at the fog places, `repeat` |
| 2 | Integrate pass: segmented scan, 4 threads a column of 16 slices, combined by subgroup ops | `column` occupancy 21% → ~85%; ~0.03–0.06 ms (estimate) | experiment | ulp-level change. Proof: `shot --against`, `repeat` |
| 3 | SPD (one dispatch) for the wave and ripple mip chains instead of blit-per-level | ~19 queue drains a frame; ~0.03–0.06 ms of `waves`+`ripples` (estimate; confirm with `nsys`) | straightforward-to-medium | Box mean as now. Proof: `shot --against` (the store rounding may differ from the blit's), `kernels` |
| 4 | Ripple field RG32F ping-pong → three R32F images | 16 → 12 MB; step traffic 16 → 12 B/texel, compose reads halved | straightforward | Bit-identical. Proof: `shot --against` exact, `repeat` |
| 5 | One world sprite bin instead of one per frame slot (report buffer stays per slot) | One full set of sprite tables, including the tile list at its high-water mark (up to 64 MB in a storm) | straightforward, verify | None if the head-barrier argument holds. Proof: `repeat`, `check` in the storm suite |
| 6 | Transient aliasing: bloom over FSR's dead transients or the fog transients; FSR transients over the denoiser's | Bloom 9.8 MB now; ~37 MB more once the denoiser side joins | experiment (infrastructure) | Barriers. Proof: `repeat`, validation |
| 7 | Bloom pyramid alpha: RGBA16F (alpha written 1.0) → B10G11R11 with a nearest-rounded store | 9.8 → 4.9 MB; half the bloom traffic; ≤0.02 ms | experiment | Needs pre-rounding, or the RTZ bias darkens the veil. Proof: `shot --against` |
| 8 | Histogram fused into the bloom's frame halving | 29.5 MB read; ~0.01–0.02 ms | straightforward | Same pixels counted. Proof: exposure tests, `shot --against` exact |
| 9 | Fog `seeing` written only in frames with puffs; `airSunward`+`sliceSunward` merged into one RG16F image | 13 MB of writes on puff-free frames; one image fewer | straightforward | Worst case unchanged, so low value |

Details follow, pass by pass.

---

## 1. The air volume (`air` 0.41 ms, `column` 0.11 ms)

Files:
- `components/rtxvulkan/trace/fogvolume.{hpp,cpp}`
- `shaders/trace/fogdepth.rgen`, `fogscatter.rgen`, `fogintegrate.comp`
- `shaders/lib/fog.glsl`, `lib/froxel.glsl`
- `shaders/shared/fogvolume.h`
- the constants in `components/rtx/shaders/scene.h:242,249` and `look.h:1496`

### What it owns

Grid: `ceil(w/8) x ceil(h/8) x 64` (`FOG_VOLUME_SCALE = 8`, `FOG_VOLUME_SLICES = 64`). At 1707x960 that is
214x120x64 = 1,643,520 froxels: exactly one froxel per traced pixel.

| image | format | MB @1707x960 | MB @2560x1440 traced (native) | lifetime |
|---|---|---|---|---|
| scatter x2 | RGBA32F | 52.6 | 118.0 | history pair |
| sunward x2 | RGBA32F | 52.6 | 118.0 | history pair |
| lamps | RGBA16F (a = 0) | 13.1 | 29.5 | frame |
| air | RGBA16F | 13.1 | 29.5 | frame |
| air sunward | R16F | 3.3 | 7.4 | frame |
| slice | RGBA16F | 13.1 | 29.5 | frame |
| slice sunward | R16F | 3.3 | 7.4 | frame |
| seeing | RGBA16F | 13.1 | 29.5 | frame (puffs only) |
| column depth, column moons | RG32F, RGBA32F x2 | 1.0 | 2.3 | frame |
| **total** | | **165.4** | **370.9** | |

That is 100 bytes per froxel, 64 of them history. Published practice:
- Frostbite (Hillaire 2015): a V-buffer of two RGBA16F texels per froxel, then a scattering volume and
  an integrated volume, each RGBA16F.
- UE4's volumetric fog (Wright 2017): VBufferA/B, light scattering with a history, and the integrated
  volume, all RGBA16F.
- Either way that is about 40 B per froxel, and the history in halves.

The fork carries more channels for good reasons (the sun's transport kept apart, three "seen" answers,
the lamps' integral apart), so the channel count is not the finding. The width of the history is.

Grid size against practice:
- Wronski's AC4 fog: 160x90x64 regardless of resolution.
- Frostbite and UE: 1/8 of the screen in xy. The fork matches this, at the *traced* extent.
- RTX Remix, the path-traced comparison: a froxel grid of 1/16 of the render resolution and 48 slices,
  with a quadratic distribution (`rtx.froxelGridResolutionScale = 16`, `froxelDepthSlices = 48`,
  exponent 2.0), one visibility ray per froxel, and up to 254 frames of accumulation.
- The fork casts 2–4 rays per froxel (sun, moon pick, lamp, and ambient when puffs are in the frame)
  on 5.3x as many froxels as Remix would, with a history of about 10 frames (0.9).

### Finding 1: the history in halves, with the rounding made explicit (experiment)

**Where:**
- `shared/fogvolume.h:30` (`FOG_HISTORY_FORMAT STORAGE_RGBA32F`)
- `fogvolume.cpp:120-133`
- the stores at `fogscatter.rgen:306-307`

**Why the history is 32-bit now:** `storageformat.h:55` (`mayRoundTowardNought`) and the header's own
note. A half store on this card rounds toward zero, and the blend `mix(new, old, 0.9)` compounds that to
about 0.3% under.

**The fix:** that is a property of the *store's conversion*, not of the half. If the shader rounds the
float to the nearest half itself, the stored value is exactly representable and the conversion has
nothing to round. Whatever mode the store uses then agrees.
- Integer RNE on the float's bits: add `0x0FFF + ((bits >> 13) & 1)` and mask the low 13 bits, with
  the half's subnormal range handled.
- Pure integer arithmetic, so the SPIR-V pinning has nothing to pin, and both vendors agree.
- `RtxHalfStoreTest` is the probe that proves the store is then exact.

**Plain RNE leaves a dead band.** The history stops moving when `|0.1·(x − h)| < ½ ulp(h)`, so a
converged froxel can sit up to 5 half-ulps (≈0.24%) from its mean. That error has no sign, but it is the
same size as the bias the 32-bit format was chosen to avoid. **Stochastic rounding** removes it: add a
blue-noise offset in [−½, ½) ulp before the RNE.
- It is unbiased in expectation; see Croci et al. 2022, the standard analysis.
- Its noise is σ ≈ 0.29 ulp ≈ 0.014%, far below a display step.
- It stays deterministic, because the blue noise is a function of the frame and the froxel, so
  `repeat` still holds.
- The same helper would let the denoiser's fed-back histories (`DenoiseHistory`, which
  `mayRoundTowardNought` also guards) drop to halves. That belongs to the denoiser's audit, but the
  helper is one piece of code.

**Saving:**
- 52.6 MB at 1707x960, 118 MB at native 2560x1440.
- Traffic: the scatter pass writes 16 B less a froxel, its trilinear history read is 16 B less, and the
  integrate pass reads 16 B less. About 48 B x 1.64 M ≈ 79 MB a frame, roughly a third of the volume's
  traffic.
- Time: a guess of 0.05–0.1 ms across `air` and `column`, since `air` is ray-bound and `column`
  fetch-bound. Needs `bench`.

**Risk and proof:**
- The air moves by under one half ulp at a store.
- `./omw release noise --ab=<switch>` at the fog places (`balmora-fog-night`, `seyda-neen-ship-dawn`),
  then `shot --against` across all places, then `repeat --pairs=10`.
- A mean-level check: hold the converged volume against the 32-bit build. `check` already asserts what
  the tree claims about the air.

### Finding 2: a segmented scan down the column (experiment)

**Where:** `fogintegrate.comp`, with the `FOG_COLUMN_WORKGROUP` comment at `shared/fogvolume.h`
("a thread to a column … not a shape to be improved").

**Why the comment does not hold:**
- `fogThrough` (`shared/medium.h:97-107`) is affine in the column state: `S' = S + T·a`,
  `Sun' = Sun + T·b`, `T' = T·c`. Affine maps compose associatively, so the front-to-back scan is a
  prefix scan and parallelises the textbook way (Blelloch). Four threads a column each integrate 16
  slices from `(0, 0, 1)`, then combine the four partial maps with three subgroup shuffles and apply
  the prefix.
- Today the dispatch is 214x120 = 25.7 K threads. On 76 SMs that is about 330 threads an SM, about 21%
  occupancy, and every thread walks 64 dependent iterations of the 18-fetch tent.

**Saving:** 4x the threads, so roughly 85% occupancy. Estimate 0.03–0.06 ms off `column`.

**Risk and proof:**
- The float order changes, because `T` is now a product of segment transmittances. The result is
  still pinned and deterministic, and differs from today at the ulp level.
- `shot --against` and `repeat`.

### Smaller fog items

- **`seeing` is written every frame and read only by puffs.**
  - Where: `fogintegrate.comp:156`, with `puffLight` in `lib/sprites.glsl:103`.
  - A uniform `if (frame.mPuffsInFrame != 0u)` around that store is not divergence; `mPuffsInFrame`
    already gates the ambient ray this way (`fogscatter.rgen`).
  - Saves 13 MB of writes on puff-free frames. It does nothing for the worst case, so it is low value.
- **`airSunward` and `sliceSunward` are two R16F images indexed by the same froxel.**
  - Merged into one RG16F image, the set drops from 11 images and 19 bindings to 10 and 17.
  - The trace's two fetches read 4 B instead of 2 B each, so traffic is neutral to slightly worse.
  - A simplification only.
- **`lamps` stores `vec4(lampScatter, 0.0)`** (`fogscatter.rgen:308`): 2 B a froxel of zeros. No
  3-channel storage format is portable, so leave it.
- **Froxels behind every pixel's surface.**
  - In an interior almost all slices are behind the walls: at about 1000 units, `sqrt(1000/30000)`
    puts the wall in slice 11 of 64. Yet each froxel still casts its rays (`fogscatter.rgen`, "drawn
    over its whole slice, every frame"), and `air` is 0.227 ms in `balmora-mages-guild`.
  - The file's reason is sound: a pixel beside a silhouette sees past its column's one jittered depth
    sample, and next frame's reprojection reads these froxels.
  - A conservative cull needs a per-block *maximum* depth. Only the previous frame's G-buffer has one
    before this frame is traced, so the cull would be a heuristic that fails at disocclusions.
  - **Not recommended** under the correctness-first rule unless a measured interior case justifies an
    experiment with a refreshed-at-a-fixed-rate fallback.

---

## 2. The sea (`waves` 0.135 ms) and the wake (`ripples` 0.11 ms)

Files:
- `trace/wavepass.{hpp,cpp}`, `shaders/trace/waverows.comp`, `wavecolumns.comp`,
  `lib/wavelines.glsl`
- `trace/ripplepass.{hpp,cpp}`, `shaders/trace/ripplestep.comp`, `ripplecompose.comp`,
  `shared/ripple.h`

### What they own

- **Waves** (`sWaveTiles`: 512² over 4096 u and 128² over 1519 u; `wavecascade.hpp:40`):
  - surface and curvature: RGBA16F with full chains, 5.9 MB;
  - the transform's field buffer: 6.7 MB;
  - amplitudes and turn rates: 3.3 MB.
  - About 16 MB in all. Not oversized: 8 units (≈11 cm) a texel. Tessendorf-style FFT oceans use
    256–512 per cascade.
- **Ripples** (`RIPPLE_GRID = 1024`, 2.5 u a texel, the rasterizer's own field):
  - two RG32F fields: 16.8 MB;
  - surface and curvature: RGBA16F with chains, 22.4 MB.
  - About 39 MB.

### Finding 3: SPD for the mip chains (straightforward to medium)

**Where:**
- `Image::buildMips`, `device/memory/image.cpp:343`
- called from `wavepass.cpp:188` (four images; deepest chain 10 levels) and `ripplepass.cpp:212`
  (two images, 11 levels)

**Why:**
- `buildMips` records one `vkCmdBlitImage` per image per level, and a barrier per level, because each
  level reads the one above it.
- That is 9 drains for the waves and 10 for the wake every frame. The levels below 32² are a few
  hundred texels each, so the GPU sits nearly idle through most of them while each drain still costs
  a few microseconds.
- AMD's FidelityFX Single Pass Downsampler builds up to 12 levels of a 4096² image in **one** dispatch:
  LDS for the first levels, then a global atomic so the last workgroup builds the tail.
- The reduction these chains need is the plain 2x2 mean. LEAN wants the mean of the value and the mean
  of its square, and both are box means, which is exactly SPD's default reduction.

**Better still:** fuse `ripplecompose.comp` into SPD's first stage. Each thread computes its 2x2 mip-0
texels from the field's finite differences, stores them, and reduces. The compose dispatch and its
barrier go away.

**Saving:** about 19 drains and 77 small blits a frame. Estimate 0.03–0.06 ms of the 0.245 ms the two
zones take, and the zones are paid on every frame with water. Confirm the share with
`nsys profile --gpuctxsw=true` before claiming it.

**Precision:** the blit's rounding into RGBA16F is the driver's, just as the store's is. Under SPD the
shader owns it. Pre-round to nearest (finding 1's helper), because LEAN's variance `E[x²] − E[x]²` is
the difference of two rounded means, and a one-signed bias in either one is a bias in roughness.

**Proof:** `kernels`, then `shot --against` at the sea places (`seyda-neen-ship*`) and `repeat`.

### Finding 4: the wake field stores each height twice (straightforward)

**Where:**
- `shared/ripple.h:42` (`RIPPLE_FIELD_FORMAT STORAGE_RG32F`)
- `ripplestep.comp:86` (`vec4(heights …)` with `heights = vec2(next, held.x)`)
- `ripplecompose.comp` reads `.x` only

**Why:** the `.y` written this step is the `.x` of the image the step read. A three-image R32F ring
holds the same state: `h[n-1]`, `h[n]`, and `h[n+1]` written. The step reads `h[n]` (5 taps) and
`h[n-1]` (its own texel, at the window's shift on the first step, as now) and writes `h[n+1]`.

**Saving:**
- 16.8 → 12.6 MB.
- Step traffic 16 → 12 B a texel at every substep.
- The compose's 9 taps read 4 B instead of 8.

**Picture:** identical bits, since the same floats go through the same arithmetic. `shot --against`
must report nothing and `repeat` must pass.

### Smaller sea items

- **Impulses.** `ripplestep.comp`'s impulse loop runs every texel through every impulse, up to 128, on
  the first substep. An impulse reaches only `2 × radius` (`pressed`: `kept` saturates at 1 for
  `away ≥ 2`). Each workgroup could test the impulses against its 16x16 rectangle once, a uniform
  branch, and keep the hits in LDS. That cuts a 128-impulse frame from about 134 M distance evaluations
  to a few thousand. Worst case only, and low value at today's counts.
- **Twiddles.** `lib/wavelines.glsl:60` calls `turnedBy(…, angle)` once per field per butterfly with one
  angle. The compiler probably CSEs the sincos. If it does not, hoist it, or read twiddles from a small
  table. Check in the SPIR-V before touching it.

---

## 3. The sky (`lib/sky.glsl`, `lib/starfield.glsl`)

No image of its own: it is evaluated in the trace's miss path and, for the star field, per pixel in
`tone.comp`. Nothing to cut here in memory. Its time belongs to `trace` and `tone`.

---

## 4. Sprites and puffs (`puffs` 0.08 ms, 0.29 in the storm; `sprites` 0.02; `emitters` 0.016; `shade` 0.04)

Files:
- `trace/spritebin.{hpp,cpp}`, `trace/spritepasses.cpp`
- `shaders/scene/sprite*.comp`, `shaders/trace/sprite*.rgen`
- `lib/sprites.glsl`, `lib/spritelist.glsl`
- `components/rtx/frame/spritelistsize.hpp`

### Buffers

All `GrowableBuffer`s grown to twice the high-water mark and never shrunk:
- sprites (`GpuSprite` x count), order (count x 2 lights x 8 B), rects (count x 8 B);
- emitter frames;
- presence (one word a tile);
- the tile list: `tiles + 1` starts plus runs, sized at twice the last report, capped at 16 M entries
  (64 MB).

The sizing rule is sound: a misjudged frame falls back to `SPRITE_TILE_UNBINNED` rather than dropping
anything.

### Finding 5: the world keeps a bin per frame slot that it does not need (straightforward, verify)

**Where:**
- `tracechain.cpp:46-49`: the world's chain is made with `sFrameSlots` bins (`vulkanrenderer.cpp:128`)
- the reason given in `spritebin.hpp`'s class comment

**Why it looks redundant:**
- The reason in that comment was that the host rewrote sprites in place under a frame in flight.
  That is no longer true: `SpriteBin::take` copies on the queue (`spritebin.cpp`), and every other
  table is written by the device.
- Every command buffer opens with a full barrier (`CommandPool::begin`), so frame N+1's bin writes
  wait for frame N's last reader, `tone.comp`'s `puffsCoverNothing`.
- A growth during a frame in flight already buries the old buffer in the graveyard.
- `TraceChain`'s own constructor comment says exactly this is why the pictures get one bin.

**The one per-slot need is the report.** `mReport` is host-read once the timeline passes the submit
that wrote it. With one report and two frames always in flight, the last-named submit is never finished
at `take`, and the list would never grow. Keep one 4-byte read-back buffer per slot and one set of
tables.

**Saving:** one full set of sprite tables, including a tile list at its high-water mark. That is small
in clear weather, and tens of MB in a storm (the cap is 64 MB a copy).

**Proof:** `repeat --pairs=10` with `check` over the storm suite (`balmora-storm-night`). Also confirm
nothing else reads a bin a frame later; I found nothing that does.

### Not recommended

- **`spritecomposite.rgen:96`'s second world ray** (`surfaceDepthAt`) costs a closest-hit traversal
  per traced pixel under puffs, and is most of the storm's 0.29 ms. It exists because the surface
  channel's distance is the *jittered* sample's, and the composite must stop puffs at the unjittered
  centre of the shown pixel. Reusing the jittered depth would put the jitter on every silhouette
  behind rain. I do not recommend changing it.

---

## 5. The upscaler (`upscale` 0.37 ms)

Files: `upscale/upscaler.cpp`, `shaders/shared/fsr.h:17-32`, `shaders/upscale/`.

### The port matches the reference

Checked against the SDK's `ffx_fsr3upscaler.cpp` at v1.1.4 (`internalSurfaceDesc`):
- **Every format and extent is the SDK's**, the three "shared" images
  (`ffxFsr3UpscalerGetSharedResourceDescriptions`) included.
- **The totals agree.** Excluding the output: 131.6 MB. Less the 19.7 MB the SDK has the application
  create, that is 111.9 MB = 106.7 MiB. AMD's FSR 3.1.4 documentation lists 106 MB working set and
  89 MB persistent at 2560x1440 Quality.
- **Time is in line.** AMD's own FSR2 table gives 0.3 ms at 1440p Quality on an RX 7900 XTX, and states
  that FSR 3.1 is slightly slower than 3.0. This card measures 0.37 ms.
- **Recommendation:** do not deviate from the reference; it is the port's contract (`extern/fidelityfx/README.md`).

| image | format | MB | lifetime per SDK |
|---|---|---|---|
| history x2 (output) | RGBA16F | 59.0 | persistent |
| luma history x2 | RGBA16F | 26.2 | persistent |
| luma x2, accumulation x2 | R16F, R8 | 9.8 | persistent |
| output (app-owned) | RGBA16F | 29.5 | frame, until tone |
| dilated masks, intermediate, new locks, SPD mips, farthest mip1, shading change | — | 16.9 | **ALIASABLE** in the SDK |
| dilated depth, dilated motion, prev nearest depth | R32F, RG16F, R32UI | 19.7 | "shared"; transient here (written each frame by `Inputs`, read by later passes, `PreviousDepth` cleared every frame) |

### Finding 6: aliasing the transients (experiment, infrastructure)

The SDK's own flags say 16.9 MB is dead outside FSR's dispatch, and here the shared three are too:
36.6 MB in all. In this frame's order:
- The denoiser's transients die at the composite, **before** FSR starts. The two sets never overlap in
  time, and the denoiser's are the natural partner (the denoiser auditor's figures apply).
- The bloom pyramid (9.8 MB) starts after the puffs composite, **after** FSR's last pass. It fits inside
  FSR's dilated set (19.7 MB).
- The fog's per-frame images (59 MB) are read by the puffs composite after the upscale, so they overlap
  FSR. But they are dead before bloom, so bloom could sit in them instead.

**The mechanism:**
- Vulkan allows it with images bound to overlapping memory, the first use of each a transition from
  `UNDEFINED`. VMA documents the pattern ("Resource aliasing (overlap)").
- Frostbite's FrameGraph (O'Donnell, GDC 2017) is the published case for doing it generally.
- The tree's images already treat these as discarded each frame (`Use::sUndefined` at `begin`), so the
  barriers are there.
- What is missing is an allocator that binds two images to one range. That is the cost.

**Proof:** validation layers, `repeat --pairs=10`, `shot --against` exact.

---

## 6. The display chain (`bloom` 0.072, `exposure` 0.024, `glare` 0.003, `tone` 0.043 ms)

Files: `display/*.cpp`, `shaders/display/*.comp`, `shared/bloom.h`, `shared/exposure.h`.

### Bloom

The pyramid follows published practice:
- Jimenez's pyramid (COD:AW): a 13-tap downsample with a Karis average on the first, and a 9-tap tent
  back up, 6 levels.
- The final tent from level 0 to the frame **is already fused into `tone.comp`** (`bloomSpread`).
- An SPD-style single pass does not fit: the 13-tap footprint overlaps across tiles, while SPD reduces
  disjoint 2x2 blocks. With 11 dispatches in about 70 µs, there is little left to fuse except the
  tail: levels 3–5 are 160x90 and smaller and fit one workgroup's LDS.

**Finding 7, the alpha channel is dead (experiment).**
- `bloomdown.comp:49` stores `vec4(…, 1.0)` into `BLOOM_LEVEL STORAGE_RGBA16F` (`shared/bloom.h:93`),
  so a quarter of every bloom byte is a constant.
- `B10G11R11_UFLOAT` is a storage format under Vulkan's `shaderStorageImageExtendedFormats` (supported
  on RTX 20+ and RDNA2), and halves the pyramid: 9.8 → 4.9 MB, and about half of `bloom`'s traffic.
- **But** its mantissa is 6 bits (5 for blue). An RTZ store across about 11 chained stores (6 down,
  5 up) biases the veil low by up to several percent, which is 0.3–0.5% of a bloom-heavy picture at
  `BLOOM_STRENGTH`.
- So it needs the same explicit nearest-rounding as finding 1, and the half-store probe extended to
  this format.
- Saving ≤ 0.02 ms. **Proof:** `shot --against`, which also checks the veil's level.

**Finding 8, fuse the histogram into the frame's halving (straightforward).**
- `histogram.comp:47` reads every shown pixel (29.5 MB at 2560x1440) right after `bloomdown.comp`'s
  `KARIS` dispatch has sampled the same image.
- A halving thread owns source texels `2p … 2p+1`. Loading those four (L1-hot from its own taps) and
  binning them into the LDS histogram counts exactly the same pixels. Add an edge column and row of
  threads for an odd extent.
- Ordering is already right. The Karis halving reads *last* frame's `exposure` and the reduction writes
  this frame's afterwards (`displaychain.cpp`, bloom before exposure), so the reduce stays a separate
  tiny dispatch.
- It applies only where a frame has a pyramid; a frame without one keeps today's histogram dispatch.
- Saving: one full-frame read, about 0.01–0.02 ms. The exposure must be identical, so
  `shot --against` must report nothing.

### Sun glare, tone, GUI, digest

- **Sun glare** (0.003 ms) is a one-thread easing, and its result is already folded into tone. Folding
  it into the exposure reduce saves a dispatch and a barrier, a few microseconds.
- **Tone** reads the frame, a 9-tap bloom tent, and, only where stars, a picture or Night-Eye ask, the
  backdrop, surface and lift. It writes RGBA8. Nothing to cut.
- **GUI and present:**
  - Two RGBA8 images at the shown extent (picture and shown, 29.5 MB), one full-screen draw of the
    picture, then a blit to the swapchain.
  - The second image is required by the design: a frame with no trace must not blend the interface
    over itself, and the renderer never draws into the swapchain (`architecture.md` §8).
  - No change.
- **Digest** runs only on a harness read-back (`vulkanrenderer.cpp:685`). Not on a played frame.

---

## Checked and fine

- **The froxel grid's xy and z resolution** match Frostbite and UE (1/8 of the screen, 64 slices).
  Only the comparison with a path-traced renderer (Remix, 1/16 and 48 slices) suggests going coarser
  (finding 2).
- **FSR's formats, extents and pass order** match the SDK.
- **The bloom design** matches Jimenez, and its last tent is already in tone.
- **The sea's tile sizes** match the field's practice.
- **The sprite list sizing** is sound.
- **The ripple step** runs every frame by the water's clock (an accepted diff).

## Sources

- Hillaire, *Physically Based and Unified Volumetric Rendering in Frostbite*, SIGGRAPH 2015 Advances: https://www.slideshare.net/DICEStudio/physically-based-and-unified-volumetric-rendering-in-frostbite and https://www.ea.com/news/physically-based-unified-volumetric-rendering-in-frostbite
- Wronski, *Volumetric Fog: Unified compute shader based solution to atmospheric scattering* (AC4), SIGGRAPH 2014 Advances: https://bartwronski.com/publications/
- Wright, *Volumetric Fog and Lighting* (UE4), SIGGRAPH 2017 Advances: https://advances.realtimerendering.com/s2017/
- Unreal Engine volumetric fog documentation: https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine
- NVIDIA RTX Remix volumetrics settings (froxel scale 16, 48 slices, exponent 2, 1 visibility ray per froxel, 254-frame accumulation): https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.4.0-0/docs/runtimeinterface/renderingtab/remix-runtimeinterface-rendering-volumetrics.html
- AMD FSR 3.1.4 upscaler manual (memory table, aliasable vs persistent): https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/
- AMD FSR2 manual (performance table at 1440p): https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/
- FidelityFX SDK v1.1.4 `ffx_fsr3upscaler.cpp` (resource formats, `FFX_RESOURCE_FLAGS_ALIASABLE`): https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
- AMD FidelityFX Single Pass Downsampler: https://gpuopen.com/fidelityfx-spd/
- Jimenez, *Next Generation Post Processing in Call of Duty: Advanced Warfare*, SIGGRAPH 2014 Advances: https://advances.realtimerendering.com/s2014/
- O'Donnell, *FrameGraph: Extensible Rendering Architecture in Frostbite*, GDC 2017: https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in
- VMA, resource aliasing: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
- Blelloch, *Prefix Sums and Their Applications* (1990): https://www.cs.cmu.edu/~guyb/papers/Ble93.pdf
- Croci et al., *Stochastic rounding: implementation, error analysis and applications*, R. Soc. Open Sci. 2022: https://royalsocietypublishing.org/doi/10.1098/rsos.211631
- Vulkan specification, `shaderStorageImageExtendedFormats`: https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#features-shaderStorageImageExtendedFormats

Caveats: millisecond savings are estimates from the bytes and thread counts above, not measurements.
Each needs `./omw release bench` on a hot card, read as medians and the p99 per the project's
measurement rules. UE's exact console-variable defaults were not confirmed, so none are quoted.
