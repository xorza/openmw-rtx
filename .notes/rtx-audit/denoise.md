# Denoiser audit: memory, traffic and image count

Scope: `components/rtxvulkan/trace/denoise/*`, `shaders/trace/denoise/*.comp`,
`shaders/shared/{accumulate,atrous,shadow,specular,pane,historyclamp,composite}.h`, and the libs they
lean on (`lib/sharedexponent.glsl`, `lib/runningmean.glsl`, `lib/historyclamp.glsl`,
`lib/surfacematch.glsl`). This was a read-only audit: nothing was built, run or measured. Every
byte count below comes from reading the code. Every millisecond figure is either the measured zone
median from `measured.json` or an estimate, and an estimate says it is one.

## 0. Bottom line

1. **Half the history can go without moving the picture by more than a rounding.** The fix for this
   card's toward-nought half store is not full floats. Round in the ALU to a value a half holds
   exactly, and the store keeps it under either mode. The Vulkan spec guarantees this: "finite values
   falling between two representable finite values are rounded… The rounding mode is not defined",
   so a value that *is* representable is stored as itself. The tree already does this deterministically for
   the pane albedo (`visibility.rgen:479-485`, `& 0xffffe000u`). For a running mean the rounding has
   to be **stochastic** and not merely to nearest, and §2 says why. The glossy and pane means are
   kept so already (`roundedToHalf`). The accumulator and wavelet's 200 B/px drop to about 96 B/px,
   the shadow fields' 2 × 48 to 52, and the pane's held surface moves to the G-buffer (§3–§5). The
   total goes from **328 to about 220 B/px** with the G-buffer's +16.
2. **Time.** The filter zone (0.59–0.74 ms) is the largest. The code says it is ALU- and TEX-bound
   rather than DRAM-bound. The lever there is NRD's own `RELAX_AtrousSmem` shape: a first level that
   decodes each texel's normal and position **once into shared memory**, where today each is decoded
   twenty-five times over. Add the packed bounce+fill texel, which halves the level's fetch count. The earlier "LDS did not pay" note was about the *strided* levels with
   Dolp's permutation, which is a different experiment.

Measured denoiser share (zone medians, `measured.json`; filter + shadow + accumulate + clamp +
composite + pane):

| place | denoise ms | frame ms | share |
|---|---|---|---|
| seyda-neen-ship | 1.52 | 6.83 | 22% |
| seyda-neen-ship-dawn | 1.59 | 7.73 | 21% |
| balmora-mages-guild | 1.68 | 5.49 | 31% |
| seyda-neen-ship-overcast | 1.51 | 6.81 | 22% |
| balmora-fog-night | 1.47 | 5.40 | 27% |
| balmora-storm-night | 1.26 | 5.26 | 24% |

No specular zone appears. These places are vanilla, so the glossy filter never ran in the
measurement.

## 1. What each pass moves today

As audited, before the items done since: one-image histories, one-word fast means, `R32F` shadow
visibility, the clamp's and the composite's skipped loads, and the glossy and pane means in halves.
The images total 328 B/px now.

Model: the unique bytes per pixel a pass reads and writes. Apron and neighbour overlap is assumed to
hit in cache, and DRAM traffic is roughly the unique bytes. Radiance channels are `RGBA16F` (8 B) in
a game or bench run (`GBUFFER_RADIANCE_SHOWN`), the surface is `RG32F` (8 B), and motion is
`RGBA16F` (8 B). At 1707×960 (1.639 Mpx), 1 B/px is 1.64 MB a frame, or 2.8 µs at the card's
576 GB/s. The measured zones move more than that rate allows (the accumulator's 160 B/px in
0.23 ms would be 1.1 TB/s), so a large share of these reads is served from the 64 MB L2. **Bytes do
not convert to milliseconds linearly here**, and every ms saving below is an estimate to measure.

| pass | reads (B/px) | writes (B/px) | total | taps / fetch path |
|---|---|---|---|---|
| accumulate (`accumulate.comp`) | G-buffer 32 (indirect, fill, surface, motion) + history 64 (surface 8, colour 16, moments 16, fill 16, fast 8) = 96 | blended 16, fill blended 16, surface 8, moments 16, fast 8 = 64 | **160** | 4 bilinear taps × 5 images, storage loads |
| clamp (`accumulateclamp.comp`) | surface 8, fast 8, indirect 8, fill 8, moments 16, blended 16, fill blended 16 = 80 | blended 16, fill blended 16, fast 8 = 40 | **120** | 12×12 LDS tile per 8×8 (already tiled) |
| wavelet L0 (`atrous.comp`, wide) | blended 16, fill 16, surface 8, moments 16 (for `.a` alone) = 56 | colour 16, fill 16 = 32 | **88** | 25 + 9 taps sampled; **93 fetches/px, ~1.2 KB/px through TEX** |
| wavelet L1 | colour 16, fill 16, surface 8 = 40 | narrow 8 + 8 = 16 | 56 | 9 taps × 3 fetches |
| wavelet L2, L3 | narrow 8 + 8, surface 8 = 24 | 16 | 40 each | 9 taps × 3 fetches, step 4 and 8 |
| shadow mask, per field | shadowed 8, surface 8, penumbra 2 = 18 | ~0.1 | 18 | storage |
| shadow tiles, per field (filtered tile) | surface 8, motion 8, held surface 8, history 8, moments 16 = 48 | scratch 8, moments 16 = 24 | 72 | cleared tiles skip the reads but write 24 |
| shadow filter ×3, per field | surface 8, source 8 | 8 | 24 each, 72 in all | 16×16 LDS square |
| specular + clamp (mapped only) | 56 + 40 | 24 + 24 | ~136 | |
| pane + clamp, where no layer | 16 + (fast 8 + **mean 16**) | 32 + 8 | ~80 | the pane zone is 0.047 ms at every place |
| composite | 13 images, ~120 | 8 | ~128 | |

Sum on a vanilla frame with both shadow fields: about **1036 B/px**, or 1.70 GB a frame at
1707×960.

Images: 33 rows, 46 `VkImage`s (13 pairs), at **440 B/px**: accumulator and wavelet 200, shadow
2×56, glossy 56, pane 72. NRD's own published working sets at 1080p, from the
[NRD README](https://github.com/NVIDIA-RTX/NRD), divided by 2.07 Mpx: RELAX_DIFFUSE 90.8 MB
(44 B/px: 26 persistent, 17 aliasable), RELAX_DIFFUSE_SPECULAR 168.9 MB (81 B/px), and
SIGMA_SHADOW 31.9 MB (15 B/px). The NRD comparison that matters is the structure, from
`Source/Denoisers/Relax_Diffuse.hpp`:

- History in `RGBA16_SFLOAT`, with the second moment in alpha.
- History length in `R8_UNORM`.
- **One ping/pong pair of `RGBA16_SFLOAT` through every pass**: temporal accumulation, history fix,
  clamp and all the à-trous levels.
- The guides copied to "prev" by the first à-trous pass, so the history surface is a by-product.

The README also says NRD's "internal pipeline uses FP16".

## 2. The precision remedy, evaluated

### 2.1 What the evidence says

- Vulkan leaves the rounding of a float→float16 conversion undefined
  ([spec, Floating-Point Format Conversions](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fundamentals-fp-conversion)).
  This card truncates (`RtxHalfStoreTest`). An overflow may become infinity *or* the largest
  finite value.
- A value already representable is stored exactly, whatever the mode. The tree relies on this at
  `visibility.rgen:479-485`. That comment also records that the driver folds
  `unpackHalf2x16(packHalf2x16(x))` back to `x`, so the bit mask is the only reliable round in the ALU.
- `packHalf2x16` and `unpackHalf2x16` are among the operations `spirvpin.hpp` says the spec leaves to
  the implementation ("packing and unpacking, whose precision it leaves to the implementation").

### 2.2 Why nearest is not enough for a running mean

With deterministic round-to-nearest, an update `α(x − m)` smaller than half an ulp is lost. An EMA
with α = 1/32 therefore stalls anywhere within 16 ulps of its target. In a half that is 16 × 2⁻¹¹ ≈
**0.78%**, toward wherever it started, on a noise-free input. Noisy input dithers the stall away,
but a still specular highlight under a steady lamp, or a converged fill, may not be noisy enough.
Today's truncation is the same mechanism, biased one way: 32 ulps, which is the 1.6% the header
measured.

**Stochastic rounding** rounds up with probability equal to the discarded fraction, so
`E[SR(v)] = v` exactly and nothing stalls. The added noise in a fed-back EMA has a variance of about
`var(e) / (2α)`. With `var(e) ≤ ulp²/4`, that gives a standard deviation of **≈ 2 ulp ≈ 0.1% of the
value**, roughly 0.04 of a level of 255 at mid-grey. For comparison, the noise suite's figures are
0.35–1.3 levels. References: Connolly, Higham & Mary,
[*Stochastic rounding and its probabilistic backward error analysis*](https://doi.org/10.1137/20M1334796)
(SIAM J. Sci. Comput. 2021), and Croci et al.,
[*Stochastic rounding: implementation, error analysis and applications*](https://doi.org/10.1098/rsos.211631)
(R. Soc. Open Sci. 2022), both on stagnation under round-to-nearest.

### 2.3 Welford/EMA variance instead of `E[l²] − E[l]²`

For an EMA, `S' = (1 − α)(S + α(x − μ)²)` is **algebraically identical** to the difference of
moments. Expanding `E2' − μ'²` gives exactly that. See West 1979, and Finch,
[*Incremental calculation of weighted mean and variance*](https://fanf2.user.srcf.net/hermes/doc/antiforgery/stats.pdf),
eq. 143.

- **In full floats it buys nothing.** The cancellation error is `2⁻²⁴ · E[l²]/var`, about 6 × 10⁻⁶
  even at σ/μ = 0.1.
- **It is what makes a half possible.** In a half, the difference would lose `2⁻¹¹ · (1 + μ²/σ²)`,
  which is 5% at σ/μ = 0.1 and garbage below that. Storing `S` (better, `σ = √S`, as the narrow levels
  store it) keeps the relative precision on the quantity itself.
- The bilinear gather and the clamp's 5×5 short-history mixture become
  `Σw(S + μ²) − (Σwμ)²` in fp32 ALU. That is the same mixture as today, and the cancellation stays
  in full floats.

So: **adopt it together with halving the moments, and not on its own.**

### 2.4 Aliasing

This is NRD's transient pool, its "Aliasable" column. It is real, but it is the last thing to do.
§5 shows the cheap form, reusing a dead image of the right shape, which needs no Vulkan memory
aliasing. True aliasing across formats
([VMA resource aliasing](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html))
needs:

- the discard moved from the frame's head barrier to each alias's first write;
- a lifetime column in the `sDeclared` table, with a `constexpr` check that aliased rows'
  lifetimes do not overlap.

Lifetimes, as recorded:

- `FastBlended` (accumulate → clamp), each field's shadow `Scratch` (temporal pass → L2), and
  `PaneFastBlended` (pane → its clamp) are all dead before the barrier ahead of the wavelet.
- The wavelet's narrow images live from L1 to the composite.

So the four narrow images (32 B/px today) fit onto those four (8 B each).

There is a catch. Neighbouring dispatches with no barrier between them overlap today: the clamp
and the sky mask, the specular clamp and the pane filter. Aliasing images across such a boundary
would add a barrier that serializes them.

## 3. The accumulator and the wavelet (200 B/px → ~96)

### 3.1 Current layout and its waste

- `Fill`, `FillBlended` and both `FillNarrow`s carry an alpha that is always nought. That is 16 B/px
  of stored zeros, and the 25 L0 fill taps fetch 16 B where 12 are used.
- `Moments.b` holds nothing (`accumulate.comp:67-68`), and `Moments` is read whole (16 B) by L0 for
  `.a` alone (`atrous.comp:208`).
- `Colour.a` holds the variance for L1, which the accumulator never reads back.
- `Surface` (`RGBA16F` pair) is a re-encoded copy of last frame's `CHANNEL_SURFACE`
  (`heldSurfaceOf`, `surfacematch.glsl:21`). It needs `mDistanceScale` only because it is a half.
- Both narrow pairs exist only to ping-pong, while `Blended` and `FillBlended` are dead after L0.

### 3.2 Proposed layout ("B2", packed)

Every slow value in the loop is written with stochastic rounding (§2).

| image | format | contents | B/px |
|---|---|---|---|
| history (pair) | `RGBA32UI`, 8 halves | bounce rgb, fill rgb, σ for L1, frames | 32 |
| moments (pair) | `R32UI`, 2 halves | μ_l, σ_l (EMA form, §2.3) | 8 |
| blended | `RGBA32UI` | as the history; the clamp writes σ | 16 |
| narrow | `RGBA32UI` | L1 and L3 write it; **L2 writes `blended`**, dead after L0 | 16 |
| fast (pair) + fast blended | `RG32UI` RGB9E5, unchanged | | 24 |
| held surface | **moved to the G-buffer**: `CHANNEL_SURFACE` as a pair (+8 there) | | 0 |
| total | | | **96** (+8 G-buffer) |

Why this is sound:

- **Frames ride in the texel the accumulator already gathers.** L0 copies them from `blended`
  into the history, so the history fix (`atrous.comp:208`) and the clamp (`accumulateclamp.comp:220`)
  read them from the texel they already fetch. That removes 16 B/px at L0 and 16 at the clamp.
- **The surface moved to the G-buffer.** Last frame's `CHANNEL_SURFACE` holds the same normal and
  the distance in full float, plus the eye flag. The accumulator's 8 B/px write goes away,
  `HistoryConstants::mDistanceScale` and `ACCUMULATE_DISTANCE_RANGE` go with it, and
  `heldSurfaceMatches` compares at fp32 distance. RELAX does the equivalent: its first à-trous pass
  writes `NORMAL_ROUGHNESS_PREV` and `VIEWZ_PREV`. The same applies to `PaneHeld` and
  `CHANNEL_PANE_SURFACE`.
- **One pipeline layout for L1..L3** (`utexture2D` sources, sampled as today). The composite then
  reads one 16 B texel for bounce and fill.

Traffic per pixel (current → proposed):

| pass | current | proposed |
|---|---|---|
| accumulate | 160 | ~96 (reads 32 + 8 + 16 + 4 + 8; writes 16 + 4 + 8) |
| clamp | 120 | ~76 |
| L0 | 88 | ~40 |
| L1 | 56 | 40 |
| L2, L3 | 40 each | 40 each |

On TEX, L0 goes from 93 to 68 fetches and from ~1.2 KB to ~0.8 KB per pixel. The narrow levels go
from 27 to 18 fetches per pixel.

A simpler "B1" keeps separate `RGBA16F` bounce and fill images with stochastic rounding, puts frames
in `fill.a` and the moments in `RG16F`. It comes to about 104 B/px (120 without the surface move).
It keeps the formatless composite, but it keeps two fetches a tap.

### 3.3 Risk and proof

- **Not bit-exact.** The history gains about 0.1% of SR noise and keeps no bias.
- **Tests:** the helper is held by `RtxHalfStoreTest`; the tests that hold a mean to the float's
  rounding move to `halfRoundedMeanError`; `RtxBounceTrailTest`, and
  `theFloorAMovingBarUncoversStartsWithTheHistoryBesideIt`.
- **Harness runs:** `./omw release noise --ab=<switch>`, starting with `--still` and then the full
  suite (bias and fireflies, `balmora-fog-night` for dim light); `./omw repeat --pairs=10`;
  `./omw shot --against` (small differences everywhere are expected, so read them);
  `./omw kernels --against`; then `bench`.

This is an experiment and a large change. Do the frames move and the narrow reuse first, because
they are format-neutral (§5).

## 4. The shadow denoiser (2 × 48 → 2 × 26 B/px)

| image | today | proposed | why it is safe |
|---|---|---|---|
| `History`, `Scratch` (mean ∈ [0,1], variance or −1) | `RG32F` 8 each | `R32UI`: mean as unorm16 by `round` (pinned to `RoundEven`), variance as a half | Even toward-nought unorm16 would bias at most 1/α = 20 steps = 3 × 10⁻⁴. Round-to-nearest stalls within 10 steps. 0 and 1 stay exact, as cleared tiles need. |
| `Moments` (pair: mean, M2, count; `.w` unused) | `RGBA32F` 16 each | `RG32UI`: μ and S as unorm16 (S ≤ ¼ for a bit), count as a half capped (say 1024) | **A behaviour change.** Welford's M2 grows with an uncapped count, so capping the count needs the EMA form, where S is stored rather than M2. The SDK's `R11G11B10` moments already stall their count near 128. |
| two fields' `Scratch`, `Tiles`, `Penumbra`, `Mask` | one each per field | one shared set | The fields already serialize through their barriers (§7). This conflicts with §7's interleave, so pick one. |

**Savings:**

- Packed history and scratch: −16 B/px (26 / 59 MB). Straightforward. Proofs:
  `RtxPenumbraDenoiseTest`,
  `theShadowDenoiserTakesTheNoiseOffAPenumbraAndLeavesItsLightWhereItWas`, `noise`, `shot`.
- Moments: −32 B/px (52 / 118 MB). An experiment. Proofs: `noise --strafe/--walk/--cut`, and the
  penumbra tests.

Traffic per filtered tile and field: tiles 72 → ~48, levels 72 → ~46.

**Fusing the mask into the tiles pass: not recommended.** The mask exists so that the
classification reads words and not 9× the G-buffer (0.24 ms recorded in `shadowmask.comp`). The
trace could OR the bits itself, but the raygen's lane layout is unspecified, so that is an
experiment outside this area.

## 5. The glossy and pane filters (24 + 40 B/px)

- **`PaneHeld` → last frame's `CHANNEL_PANE_SURFACE`.** −16 B/px here and +8 in the G-buffer.
- **Sharing one history structure between the two filters: no.** A pixel can hold a pane in front
  of a glossy surface, so their per-pixel state does not overlap. They already share
  `runningmean.glsl` and the clamp pipeline. Fusing their dispatches (one temporal kernel, one clamp
  over both) removes two dispatches and one barrier when a scene is mapped, and nothing otherwise.
  It is low priority.
- **The pane filter runs on every frame and writes ~40 B/px of zeros** where no layer stands. A
  tile classification would pay, NRD-style (`RELAX_ClassifyTiles`), or an indirect dispatch over
  tiles that hold a layer now or held one last frame. The worst case (a full-screen window) costs
  what today's every frame costs. That is at most 0.047 ms, so it is low priority.

## 6. Fuse the composite into the last wavelet level

An experiment. L3 writes the frame through `composedLight` (`lib/compose.glsl`), saving the 16 B/px
write and read of the last narrow image plus one dispatch and its barrier. The standalone composite
stays for undenoised frames, with one rule in both places. The fused level binds 13 more images, so
check its register count.

## 7. Dispatch shape and barriers

- **Every `vkCmdPipelineBarrier2` with a compute source stage waits for all earlier compute work**:
  the execution dependency is by stage, and only the memory dependency is by image. So the shadow
  pass's eight barriers (four per field, `shadowpass.cpp:86-89,120`) drain the whole queue.
  `denoisepasses.cpp:99-101` hopes the shadow, glossy and pane passes overlap, but they only
  overlap with their immediate neighbours.
  - Recording by stage instead keeps the same dependencies with half the drains: both masks; then
    both tiles passes, the specular filter and the pane filter; then both L0s and both clamps; then
    both L1s; then both L2s.
  - Estimate: 4–6 drains of a few µs each, so 0.01–0.03 ms. An experiment.
  - Reference: [AMD, Vulkan barriers explained](https://gpuopen.com/learn/vulkan-barriers-explained/),
    [RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/).
  - Mind `docs/rtx/architecture.md`'s note that the card's ulp nondeterminism follows a pipeline
    drain. More overlap changes when drains happen.
- **The first wavelet level in shared memory** (NRD `RELAX_AtrousSmem.cs.hlsl`, which keeps
  `s_Normal_Roughness`, `s_WorldPos_MaterialID` and the signal in `groupshared` for the first
  à-trous pass).
  - Load a 12×12 tile once: the decoded normal, the position (fp32, relative to the tile), and the
    packed bounce+fill.
  - That turns 25 `rayAt` + octahedral decodes and 93 fetches per pixel into about 2.25 decodes and
    about 7 fetches, at roughly 40 B of LDS per tap.
  - Rough order: L0 from ~0.3 ms toward ~0.15–0.2 ms. That is an estimate from TEX rate
    (76 SMs × 4 texels/clk) and LDS rate (128 B/clk/SM).
  - `atrouspass.cpp`'s note records that a tile with Dolp's permutation did not pay. That was the
    strided levels, where nothing is reused: at step 8, a group's 576 taps land on 576 distinct
    texels. L0 at step 1 reuses each texel about 25 times.
  - An experiment. Prove with `kernels` (bit-exact is reachable if positions are rebuilt by the
    same `rayAt`), `shot --against`, `bench`, and `nsys` on the filter zone. Time each level
    separately first; the zone does not split them today.
- **The strided levels**: there is no reuse to tile. At 1707×960 the narrow working set (13 + 13 +
  13 MB) sits in L2. At 2560×1440 native it is about 88 MB and no longer fits, and NVIDIA's
  [thread-group ID swizzling](https://developer.nvidia.com/blog/optimizing-compute-shaders-for-l2-locality-using-thread-group-id-swizzling/)
  is the documented remedy. Measure first.
- **Fusing the accumulator and its clamp: no.** The clamp needs the 5×5 neighbours' fast blends,
  which needs their reprojection, so the fused kernel repeats 2.25× of the accumulator's gathers to
  save about 76 B/px. The same holds for clamp + L0, a 9×9 dependency. ReLAX keeps these passes
  apart for the same reason
  ([NRD](https://github.com/NVIDIA-RTX/NRD), `RELAX_TemporalAccumulation`, `RELAX_HistoryClamping`).
- **Shadow mask + tiles: no** (§4).
- **Narrow levels at reduced resolution** (filter L2/L3 on a half-resolution grid with a joint
  bilateral upsample, as A-SVGF filters its gradients on a strata grid):
  [Schied et al. 2018](https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/opt-gradient.pdf).
  This changes the filter's support and the look. It is research, not a saving to bank, and only
  worth it if the per-level timing shows L2 and L3 are a large share.

## 8. Ranked list

| # | what | where | saving | picture | kind |
|---|---|---|---|---|---|
| 1 | Narrow ping-pong through `Blended` | `atrouspass.cpp:101-149` | −16 B/px memory, +16 B/px traffic in today's formats; free under #3 | none | straightforward |
| 2 | Shadow history and scratch packed (unorm16 + half) | `shadow.h:49`, `shadowtiles.comp`, `shadowfilter.comp` | −16 B/px | ≤ 3e-4 | experiment-light |
| 3 | Accumulator "B2" packed halves with stochastic rounding + EMA variance + frames in the texel | `accumulate.h`, `atrous.h`, the 4 shaders | 200 → 96 B/px; accumulate, clamp and L0 traffic −40 to −55% | ~0.1% SR noise, no bias | experiment |
| 4 | L0 in shared memory (`RELAX_AtrousSmem`) | `atrous.comp` | est. −0.1 to −0.15 ms | bit-exact reachable | experiment |
| 5 | Held surfaces → G-buffer pairs | `GBuffer`, `surfacematch.glsl`, `accumulate.comp:216,314`, `pane.comp:113` | −16 B/px net, −16 B/px writes; removes `mDistanceScale` | fp32 distances | experiment |
| 6 | Shadow moments in the EMA form, capped | `shadowtiles.comp:265-290` | −32 B/px | behaviour change | experiment |
| 7 | Barrier interleave across the shadow fields and filters | `shadowpass.cpp`, `denoisepasses.cpp` | est. 0.01–0.03 ms | none | experiment |
| 8 | Composite fused into L3 | `atrous.comp`, `compositepass.cpp` | −16 B/px, one dispatch | none | experiment |
| 9 | True aliasing of the narrow images onto the dead scratch | `denoisehistory.cpp` (a lifetime column) | −32 B/px today; less after #1 and #3 | none | last |

Combined memory with #2, #3, #5 and #6: **328 → ~220 B/px** including the G-buffer's +16.
Every one of these needs `./omw test` and finally `./omw gate`. Every change that moves a picture needs the `noise` suite as
the verdict, after a narrowed A/B (`--views=… --strafe=0 --walk=0 --still`).

## Sources

- NVIDIA NRD: [README memory table](https://github.com/NVIDIA-RTX/NRD); `Source/Denoisers/Relax_Diffuse.hpp`, `Sigma_Shadow.hpp`, `Shaders/RELAX_AtrousSmem.cs.hlsl`, `Shaders/RELAX_ClassifyTiles.cs.hlsl` (same repository).
- [Vulkan spec, floating-point and normalized conversions](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fundamentals-fp-conversion).
- Schied et al., [SVGF, HPG 2017](https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced); [A-SVGF, 2018](https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/opt-gradient.pdf).
- AMD [FidelityFX Denoiser](https://gpuopen.com/fidelityfx-denoiser/) (the shadow denoiser's SDK formats: `R11G11B10` moments, `RG16F` history).
- Connolly, Higham, Mary 2021, [doi:10.1137/20M1334796](https://doi.org/10.1137/20M1334796); Croci et al. 2022, [doi:10.1098/rsos.211631](https://doi.org/10.1098/rsos.211631).
- Finch 2009, [Incremental calculation of weighted mean and variance](https://fanf2.user.srcf.net/hermes/doc/antiforgery/stats.pdf).
- [AMD, Vulkan barriers explained](https://gpuopen.com/learn/vulkan-barriers-explained/); [AMD RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/); [NVIDIA, thread-group ID swizzling](https://developer.nvidia.com/blog/optimizing-compute-shaders-for-l2-locality-using-thread-group-id-swizzling/); [VMA resource aliasing](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html).
