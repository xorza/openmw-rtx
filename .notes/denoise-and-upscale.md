# Denoising and upscaling on NVIDIA, AMD and Intel

A plan for the denoiser and the upscaler, so that one renderer gives a clean, stable picture on
NVIDIA RTX 20 and later, AMD RDNA 2 and later, and — only after AMD is done — Intel Arc. Everything
in it is compatible with the GPLv3 and runs through Vulkan.

The plan is written so that a session can carry it out unattended. Section 10 gives the rules of
that run. Section 11 gives each phase: the files, the design, the tests, the proof and the stop rule.

The recommendation, in short:

1. **SER is removed** (phase 2, done). RADV (AMD on Linux) and ANV (Intel on Linux) can run the
   renderer.
2. **Denoise by signal, the way shipped path tracers do.** The sun's and the moons' shadows go
   through a dedicated shadow denoiser, a port of AMD's FidelityFX Shadow Denoiser (MIT). The lamps'
   diffuse light joins the bounce in the one demodulated diffuse signal that the accumulator and the
   wavelet filter. The accumulator gets a fast history against lag. ReSTIR for the lamps comes only
   if the measure asks for it. Glossy light gets a temporal filter only, when PBR content is here to
   test it.
3. **Upscale and anti-alias with FSR 3.1**, ported into the backend (MIT, analytic, every vendor).
   FSR's native mode is the renderer's anti-aliasing.
4. **No vendor ML library.** DLSS, FSR 4, FSR Ray Regeneration, XeSS and NRD all have licences that
   conflict with the GPLv3 (section 9).

All research is from September 2026. The sources are at the end.

## Status

| Phase | Work | State |
|---|---|---|
| 1 | The noise measure (`omw noise`) | done, 90768c6a9c |
| 2 | SER removal | done, 90768c6a9c |
| 3–13 | section 11 | not started |

## 1. Where the tree stands

Measured on the RTX 4090, release build, 1920×1080.

| Part | Now | Cost (GPU ms) |
|---|---|---|
| Indirect diffuse (the bounce) | `AccumulatePass` (SVGF temporal) + `AtrousPass` (5 levels) over `CHANNEL_INDIRECT` | accumulate 0.25–0.30, wavelet 1.8–2.9 |
| Sun and moon shadows | one ray across the disc per pixel (`SUN_SHADOW_RADIUS`), unfiltered | in the trace |
| Lamps | one-deep reservoir per pixel (`Reservoir`), one shadow ray, unfiltered | in the trace |
| Glossy light (PBR replacers) | one lobe draw, joins the direct channel, unfiltered | in the trace |
| Anti-aliasing | none: jitter is off by default, and no pass resolves a history | — |
| Upscaling | none: `makeUpscaler` refuses every mode but `off` | — |
| Trace | after the SER removal, the default suite's three places | 4.03, 4.47, 2.72 |

`omw release noise` (the frame against a 256-frame reference, in levels of 255):

| Place | Frame mean / p99 | Bar (16 frames averaged) mean / p99 | Verdict |
|---|---|---|---|
| `seyda-neen-pier` | 18.54 / 92 | 5.92 / 30 | noisier |
| `balmora-mages-guild` | 7.11 / 90 | 4.54 / 38 | noisier |

At the pier the noise is the sun: one shadow ray per pixel, through leaves and at penumbrae. The
wavelet filters only the bounce, so it cannot touch that noise. In the guild it is the lamps and the
bounce together.

## 2. What shipped path tracers do

The denoiser is never one filter over the whole frame. Each engine splits the light by how it varies,
reduces the variance before it filters, and filters each signal with a filter made for it.

| Renderer | Shadows | Direct lights | Diffuse | Specular | Final |
|---|---|---|---|---|---|
| Quake II RTX (NVIDIA, GPL-2.0-or-later) | in direct | light sampling | direct diffuse: A-SVGF at full size; indirect: SH filter at 1/3 size | temporal only | TAAU |
| kajiya (Embark, MIT/Apache-2.0, ran on RX 6800 XT) | FidelityFX Shadow Denoiser, 0.52 ms at 1080p | sun only | ReSTIR GI at 1/2 size + temporal, irradiance cache | ReSTIR + temporal | TAA |
| NRD titles (Cyberpunk 2077, Portal RTX) | SIGMA | RTXDI (ReSTIR DI) | ReBLUR or ReLAX | ReBLUR or ReLAX | DLSS or TAA |
| AMD Capsaicin (MIT, DirectX 12) | — | ReSTIR-style | GI-1.x radiance caches | ray traced + probes | TAA |

The practices they share:

- **Demodulate.** Filter irradiance, not radiance: divide the diffuse light by the albedo and multiply
  it back after the filter. The tree already does this for the bounce.
- **Split by frequency.** A shadow is an edge. A bounce is smooth. One filter tuned for both blurs the
  edge or leaves the bounce noisy.
- **A dedicated shadow denoiser for the sun.** It filters one number per pixel, skips tiles that are
  fully lit or fully shadowed, and keeps the lighting exact: direct sun = unshadowed sun × filtered
  visibility.
- **Reduce variance before you filter.** ReSTIR DI reuses light samples across frames and neighbours.
- **Temporal first, then spatial, with the variance as the guide.** SVGF's structure. Two refinements
  the tree lacks: a fast history that limits lag (ReLAX), and temporal gradients that reset history
  where the lighting changed (A-SVGF).
- **Glossy light: temporal only.** Q2RTX found that spatial filters fail on normal-mapped surfaces.
- **Denoise at render size, before the upscaler. Anti-alias after.** With no upscaling, the
  upscaler's native mode is the anti-aliasing.
- **Blue noise, rotated over time**, which the tree already has (`Rtx::BlueNoise`).

## 3. What is on offer

### 3.1 Denoisers

| Option | API | Licence | Fit |
|---|---|---|---|
| FidelityFX Shadow Denoiser (SDK v1.1.4, `sdk/include/FidelityFX/gpu/denoiser/ffx_denoiser_shadows_*.h`) | DX12, Vulkan (GLSL passes) | MIT | **Yes**, for the sun and the moons, as a port (4.1). |
| FidelityFX Reflection Denoiser (same SDK) | DX12, Vulkan | MIT | Later, if glossy reflections need a spatial pass. |
| A-SVGF from Quake II RTX (`asvgf.glsl`) | Vulkan, GLSL | GPL-2.0-or-later | Source to learn from and to port: the "or later" makes it GPLv3-compatible. |
| In-tree SVGF (`AccumulatePass`, `AtrousPass`) | this backend | this tree | **Yes**, extended to the combined diffuse signal. |
| ReSTIR DI (papers, SIGGRAPH 2023 course) | — | algorithm, no code | Written in-tree, if the measure asks for it. The RTXDI SDK is not used. |
| NRD (ReBLUR, ReLAX, SIGMA) | DX12, Vulkan | NVIDIA RTX SDKs licence | **No** (section 9). Its papers may guide. Its source is not read. |
| FSR Ray Regeneration 1.2 (FSR SDK 2.3) | DX12 only, RX 9000 only | signed binaries | **No**. |
| Intel Open Image Denoise | CPU, CUDA, HIP, SYCL | Apache-2.0 | **No**: not for a frame budget of a few milliseconds, and not Vulkan. |

### 3.2 Upscalers

| Option | API | Licence | Fit |
|---|---|---|---|
| FSR 3.1.4 (FidelityFX SDK v1.1.4, `sdk/include/FidelityFX/gpu/fsr3upscaler/`, `sdk/src/backends/vk/shaders/fsr3upscaler/`) | DX12, Vulkan (GLSL passes) | MIT | **Yes.** Analytic, every vendor. |
| FSR 3.1.5 (FSR SDK 2.3) | DX12; HLSL source | MIT for the `.hlsl` files | Its fixes, read from source and merged by hand later. The 2.x SDK has no Vulkan backend. |
| FSR 4.1 (FSR SDK 2.3) | DX12 only | signed DLLs, "no reverse engineering" | **No.** |
| XeSS 2 | DX11, DX12, Vulkan | Intel licence, binaries only | **No**. |
| DLSS | — | NVIDIA RTX SDKs licence | **No**; removed. |
| Snapdragon GSR 2 | GLES, Vulkan | BSD-3-Clause | No: made for mobile. |

A cross-vendor neural denoiser or upscaler on `VK_KHR_cooperative_matrix` is a research item, not a
phase: it needs a trained model and training data this plan does not have.

## 4. What the codebase changes in the earlier design

The earlier version of this plan was written from the field. Read against the tree and against the
real SDK sources, twelve points change. Each is a fact, and the design in sections 5 and 6 already
takes it in.

- **F1. The shadow denoiser cannot be vendored as it is. It is a port.** Its three filter passes
  work only with `FFX_HALF=1`: with half floats off, `ffx_denoiser_shadows_filter.h` compiles to
  stubs that return nought. The tile classification uses wave and quad operations
  (`ffxWaveAllTrue`, `ffxQuadReadX/Y`), and the mask preparation uses `ffxWaveOr` over an 8×4 tile.
  The disocclusion test reads a device depth through projection matrices, and the tree has no device
  depth. So the port keeps the algorithm and changes the arithmetic (full floats, halves only as
  storage), the reductions (shared memory, no subgroup operations) and the reprojection (the tree's
  own rule, F10).
- **F2. The sun's visibility is not one bit.** `skyVisible` is `lightThrough` (a ray that a
  translucent surface dims, `throughBlocked`) times `cloudShadow`, and under water
  `skyVisibleThrough` traces two rays. The FidelityFX denoiser filters one bit per pixel. So the bit
  is "no opaque surface stopped any of the rays", and the fraction (the translucent product, the
  cloud and the second ray's dimming) stays exact in the unshadowed term.
- **F3. The sky's sources are drawn, not summed.** `gather` picks the sun or a moon by weight
  (`pickByWeight`). At noon and at midnight the pick is whole. Only the hour either side of dusk
  divides by a chance. The unshadowed term keeps today's estimator, so that pick noise stays where
  it is now: it is not the noise the pier shows.
- **F4. A lamp has no identity from one frame to the next.** `SceneDesc::orderLights` sorts the list
  by position and intensity every frame, and a row moves when a lamp arrives or leaves. A reservoir
  carried to the next frame needs a table from last frame's rows to this frame's (section 5.4).
- **F5. There is one upscaler, and it needs no extension.** The registry, the traits and the virtual
  seam were written for several vendor libraries, and the licences removed all of them but FSR.
  FSR is compute shaders. So `upscalerextensions.hpp`, `listMissingUpscalerExtensions`,
  `sUpscalerBuilt`, `RtxSettings::playedIn` and the virtual `Upscaler` go. The render size and the
  jitter phase count are pure functions in the core (section 6.1).
- **F6. FSR reads its depth through a callback.** `LoadInputDepth` in the FSR GLSL callbacks is the
  place a port supplies the depth. The tree's callback computes the device depth from the distance in
  `CHANNEL_SURFACE` and the pixel's ray, on the frames FSR runs. No image and no pass are added.
- **F7. The harness upscales by default once an upscaler exists.** `sUpscaleByDefault` in
  `apps/rtxtool/run.hpp` becomes `Quality`. Every `shot` baseline and every `--against` run must name
  `--upscale=off` from phase 8 on, and `noise` must trace its reference and its bar at `off`.
- **F8. One thing is blocked on this machine.** There is no PBR replacer in the data (vanilla
  `Morrowind.esm`, `Tribunal.esm`, `Bloodmoon.esm` only), so the glossy filter (phase 9) has nothing
  to be measured on. The drm-shim can be built: `python-mako` is installed, the installed RADV is
  Mesa 26.2.3, and `~/Projects/mesa` stands at the tag `mesa-26.2.3`.
- **F9. The tree uses no subgroup operations.** No shader reads a subgroup, and no requirement names
  one. The ports keep it so. AMD compiles compute at wave32 or wave64 and Intel at SIMD8 to SIMD32,
  and a reduction written for an 8×4 wave is wrong on both.
- **F10. The accumulator already keeps what a reprojection needs.** `AccumulateHistory` keeps, per
  pixel, the normal and the scaled distance of the surface its mean belongs to, and
  `accumulate.comp`'s `sameSurface` says whether a history tap belongs to the surface now in front
  of the pixel. The shadow denoiser reads the same history with the same rule. There is one rule for
  "the same surface as last frame", and there is no second depth history.
- **F11. A-SVGF's temporal gradients need last frame's random sequence.** A gradient traces last
  frame's sample again at this frame's state. That needs the previous seeds per pixel, a forward
  projection of last frame's samples, and a second shading path. It is a design of its own and is
  not in the unattended run. The fast history (5.3) does most of its work for the torches.
- **F12. `noise` holds one upscaling for the whole run.** The upscaling is part of the
  `RenderProfile`. The reference and the bar must be traced at `off` and the frame at the run's mode,
  so a stop needs its own upscaling (`Schedule::mUpscale`, through `Renderer::setUpscale`).

## 5. Design: the signals

The trace writes the light in parts, each to the filter made for it. The composite puts them back
together. Water, emission, the sky and the fog stay resolved in the trace, as now.

| Signal | Written by | Filter | Channel |
|---|---|---|---|
| The sun's or a moon's light at the eye's surface, with visibility one, and whether its ray got through | `gather` for the eye's solid | shadow denoiser (5.1) over the bit | new: `CHANNEL_SUNLIT` |
| Lamps' diffuse light, demodulated | `gather` for the eye's solid | the diffuse denoiser (5.2, 5.3) | joins `CHANNEL_INDIRECT` |
| The bounce, demodulated | the trace | the diffuse denoiser | `CHANNEL_INDIRECT` |
| Glossy light of the lamps and the bounce | the trace | none now, temporal later (5.5) | `CHANNEL_DIRECT` |
| Emission, sky, water, fog, panes, what a pane or a water ray shades | the trace | none | `CHANNEL_DIRECT` |

The composite becomes

    colour = direct + sunlit.rgb × shadow + albedo × filter(indirect)

where `shadow` is the filtered visibility. Where nothing filters (`--filter=0`, a reference, a
picture inside the interface), the trace composes the frame itself, as now, with the bit in place of
`shadow`.

### 5.1 The shadow denoiser (sun and moons)

**The channel.** `CHANNEL_SUNLIT`, stored at the radiance width like `direct` and `indirect` (no format
in the shaders, `radianceFormat` on the host):

- `rgb`: what the sky's source `gather` picked would add to the pixel if its rays got through — the
  diffuse half times the albedo, and the lobe's share, whole — with the fraction of F2 already
  multiplied in, and with the path's transmittance (the water in front, the air, the panes and the
  see-through arms) as the launch applies it to the bounce. This is today's estimator with the bit
  set to one (F3).
- `a`: the bit: one where no opaque surface stopped the rays, nought where one did.

A pixel that is not the eye's lit solid (the sky, a pane's own light, water's own light, the surface
views of `--show`) writes `rgb` nought and `a` one.

**The trace.**

- `traversal.glsl`: `throughToward` returns the pair it already computes: whether a candidate was
  committed, and `throughBlocked(blocked)`. `lightThrough` stays their product, so every other
  caller keeps its arithmetic. A new `lightPassing(from, towards, distance)` returns the pair.
- `lights.glsl`: `skyVisible` gets a twin, `skyPassing`, that returns the bit and the fraction
  (`lightPassing` times `cloudShadow`). `underwater.glsl`: `skyVisibleThrough` gets the same twin, whose
  bit is the product of the two rays' bits.
- `shading.glsl`: `DirectLight` gains `vec3 mSky` (the sky source's whole contribution, per the
  channel's `rgb`, before the albedo) and `bool mSkySeen`. `gather` fills them and leaves the sky
  term out of `mDiffuse` and `mSpecular` only where the caller asks for the split: a new parameter
  `bool split`, true only from `shadeSolid` for the eye's hit. Every other caller (panes, the path's
  end, the water's reflections, the bed of a pane) composes as now.
- `shadeSolid` returns the sky term beside `direct` and `bounce`. `answerSolid` and `answerWater`
  carry it into the `Answer`. For water, the bed's term is scaled by `1 - shore`, as the bed's
  light is.
- `payload.glsl`: `Answer` and `VisibilityPayload` gain `vec3 mSunlit` (three whole floats, for the
  reason the file gives for the radiances) and the flag `ANSWER_SKY_SEEN` (bit 3). The payload grows
  from 12 words to 15. The file's comment says the count, and changes with it.
- `visibility.rgen`: after the transmittance is known, write
  `sunlit = vec4(answer.mSunlit * transmittance, seen)`. Where the frame is composed in the trace
  (`frame.mComposed`), add `answer.mSunlit * transmittance * seen` to `light` before the store.
- `gbuffer.h`: `CHANNEL_SUNLIT = 7`, `CHANNEL_COUNT = 8`. `frameimage.hpp`: `Channel::Sunlit` and its
  name `"g-sunlit"`. `DIGEST_IMAGES` follows `CHANNEL_COUNT`. `bindings.glsl` declares the image.

**The passes.** A port of the FidelityFX Shadow Denoiser v1.1.4, in
`components/rtxvulkan/trace/denoise/` beside the accumulator:

| File | What it is |
|---|---|
| `components/rtx/shaders/shadow.h` | bindings, workgroup sizes, formats and `ShadowConstants`, for both languages, like `accumulate.h` |
| `components/rtxvulkan/shaders/lib/shadowdenoise.glsl` | the ported functions of `ffx_denoiser_shadows_util.h`, `_prepare.h`, `_tileclassification.h` and `_filter.h`, with the MIT notice and a line that names the tag and says what was changed |
| `components/rtxvulkan/shaders/trace/denoise/shadowmask.comp` | pack the bits into one word per 8×4 tile (FFX `PrepareShadowMask`) |
| `components/rtxvulkan/shaders/trace/denoise/shadowtiles.comp` | tile classification, moments, temporal blend (FFX `TileClassification`) |
| `components/rtxvulkan/shaders/trace/denoise/shadowfilter.comp` | the spatial filter, one module specialised three times on its pass (step 1, 2, 4) |
| `components/rtxvulkan/trace/denoise/shadowpass.{hpp,cpp}` | the pipelines, shared by every chain, like `AccumulatePass` |
| `components/rtxvulkan/trace/denoise/shadowhistory.{hpp,cpp}` | one camera's buffers and history pairs, like `AccumulateHistory` |

What the port changes, and nothing else:

1. **Arithmetic.** Full floats everywhere. The filter's shared memory keeps halves as storage through
   `packHalf2x16`, as FFX's half path does, so the shared memory is the same size.
2. **Reductions.** `PrepareShadowMask`'s `ffxWaveOr` becomes an `atomicOr` into a shared word per
   tile. `ThreadGroupAllTrue` keeps only FFX's own shared-memory path. `GetClosestVelocity`'s quad
   reads become shared-memory reads of the 2×2 neighbours. No subgroup extension is required (F9).
3. **The guides.** The depth is the distance in `CHANNEL_SURFACE.g`, and the normal is
   `unpackSurfaceNormal(CHANNEL_SURFACE.r)`. FFX's `GetLinearDepth` through `ProjectionInverse` is
   removed. The filter's depth weight uses the plane test `atrous.comp` uses, so the tree has one
   rule for "the same surface" in a spatial filter. FFX's `DepthSimilaritySigma` goes.
4. **The receiver.** `IsShadowReciever` is "a surface is here (`packed >= 0`) and `sunlit.rgb` is not
   nought". A room has no receiver, so every tile of an interior skips at the classification.
5. **The reprojection.** FFX's `history_uv = uv + velocity` and `IsDisoccluded` go. The history tap
   is where `accumulate.comp` fetches its own (`at + 0.5 + jitter + motion`), with the same bilinear
   weights and the same `sameSurface` test against the accumulator's surface history (F10). The
   moments are fetched at the nearest accepted tap. `sameSurface` moves from `accumulate.comp` into
   a shared `lib/history.glsl`, and both passes include it.
6. **Formats.** As FFX has them: reprojection results `rg32f`, moments `rgba32f` (a pair), history
   `rg32f` (a pair), filter scratch `rg16f` (two), the output `r16f`, the mask and the tile metadata
   one `uint` per 8×4 tile each. Narrowing is a later, measured step.

**The order.** `TraceChain::recordDenoise` calls `AccumulateHistory::turn()` once (it moves out of
`AccumulatePass::record`) and hands the turn to both passes, because the shadow pass reads the
surface half the accumulator reads. Then: accumulate, shadow mask, shadow tiles, three shadow
filters, the wavelet, the composite. A barrier between each pair, as `recordDenoise` does now. One
timer zone, `"shadow"`, around the five dispatches.

**The composite.** `composite.h` gains `COMPOSITE_BIND_SUNLIT` and `COMPOSITE_BIND_SHADOW`.
`composite.comp` adds `sunlit.rgb * shadow` where it composes, and nothing where the trace composed.

**Resets.** `TraceChain::resetHistory` and a resize reset the shadow history as the accumulator's,
and the first frame after one reads no history (FFX's `IsFirstFrame`).

**Cost.** kajiya measured 0.52 ms for its sun rays and this filter together on an RX 6800 XT at
1080p. Expect the filter alone at 0.2 to 0.4 ms on the 4090. Measure before quoting.

### 5.2 The lamps join the diffuse signal

The eye's solid writes its lamps' diffuse light into `CHANNEL_INDIRECT`, demodulated, beside the
bounce. This is what NRD's RTXDI path does with ReLAX: one diffuse signal, one filter.

- `gather` keeps the lamps' diffuse term apart where `split` is set: `DirectLight::mLamps`, per unit
  albedo, net of the lobe's share (`lampDiffuse - lampDiffuse * kept.mFresnel`).
- `shadeSolid`: `bounce = bounced.mDiffuse + lit.mLamps`, and `direct` no longer holds it. The lamps'
  lobe (`kept.mSpecular * lampShare`) stays in `direct`.
- `answerWater` needs no change: the bed's bounce is whole and the albedo carries the shore.
- The unfiltered frame is the same product (`shaded + albedo × bounce`), in another order.

The accumulator's outlier clamp and variance now see the lamps. That is the intent: the variance
guides the wavelet at a lamp's penumbra.

### 5.3 The fast history

ReLAX's anti-lag, in `accumulate.comp`:

- A second mean per pixel, at `alpha = 1 / min(frames + 1, ACCUMULATE_FAST_FRAMES)` with
  `ACCUMULATE_FAST_FRAMES = 4`, kept in a new history pair `ACCUMULATE_FAST` (`STORAGE_RGBA16F`),
  reprojected with the same taps and the same `sameSurface` test as the slow mean.
- Where both are settled (`frames >= ACCUMULATE_SETTLED`), the slow mean's luminance is clamped to the
  fast mean's luminance ± `ACCUMULATE_ANTILAG_SIGMAS` × the temporal spread the moments give. Where
  the clamp moved the slow mean, its frame count is cut in proportion to how far it moved, so the
  next frames blend fast.
- `ACCUMULATE_ANTILAG_SIGMAS` starts at 2, and each constant carries its reason in `look.h`.

A torch that goes out then leaves its light in the history for a few frames, not for sixteen.

### 5.4 ReSTIR DI temporal reuse for the lamps (conditional)

Only if the guild still fails its bar after 5.1 to 5.3 (phase 5b's rule).

- **The reservoir record** per pixel: the lamp's row (`uint`), the contribution weight `W`
  (`float`), the count `M` (`float`, capped at `RESTIR_CAP = 20`), and the surface it belongs to (the
  normal code and the distance, two floats) — five words, in a buffer pair at the render extent,
  addressed through the frame block as the scene's tables are.
- **The row map.** The host writes, each frame, a `uint` per last frame's row: the row that lamp has
  now, or `~0u`. Both lists are sorted by position first (`orderLights`), so one merge on
  `mPosition` builds it, into a persistent scratch vector: no allocation on the frame path. A lamp
  that moved (a carried torch) loses its history, and that is the honest answer.
- **The combine,** in the closest-hit shader for the eye's solid: build this frame's reservoir as now
  (`weighLamps`), fetch last frame's at the motion-vector tap, reject it where `sameSurface` fails or
  the row map says the lamp is gone, re-evaluate its target (the unshadowed weight, with this frame's
  intensity — so a flicker is followed) at this surface, and merge by `considerLamp`'s rule with the
  `1/M` weights. One shadow ray for the survivor, as now.
- **The bias** is where the two surfaces' unshadowed targets differ, which `sameSurface` holds small.
  The noise measure's mean error shows a bias. The motion vector has to be known before `gather`
  runs, so `main` of the closest-hit shader computes `motionOf` first.

### 5.5 Glossy light (deferred)

Temporal only, as in Q2RTX: reproject by the surface motion, clamp to the neighbourhood, blend. Only
surfaces with a lobe write here. It waits for a PBR replacer on this machine (F8).

### 5.6 The order of a frame

```
trace → accumulate → shadow (mask, tiles, 3 filters) → wavelet → composite
      → FSR (upscale, or native AA) → puffs → display chain → GUI
```

Where nothing filters, the trace composes and only a sum runs the composite, as now.

## 6. Design: the upscaler

### 6.1 The core (`components/rtx/frame/upscale.hpp`, `reconstruction.hpp`)

The upscaler is FSR, and its sizes are arithmetic, so the core states them:

```cpp
/// What FSR 3.1 divides the output by at each mode: 1.5, 1.7, 2.0, 3.0, and 1.0 for native.
constexpr float upscaleRatio(Upscale mode);

/// What to trace at for `output` under `mode`: `uint32_t(float(output) / ratio)` per axis, which
/// is `ffxFsr3UpscalerGetRenderResolutionFromQualityMode` to the bit.
FrameExtent renderExtentFor(std::uint32_t outputWidth, std::uint32_t outputHeight, Upscale mode);

/// How many jitter phases the reconstruction cycles through: `int32_t(8.0f * pow(output / render, 2))`,
/// `ffxFsr3UpscalerGetJitterPhaseCount`.
std::uint32_t jitterPhasesFor(std::uint32_t renderWidth, std::uint32_t outputWidth);

/// What FSR's guide adds to the texture level bias past the ratio's levels.
inline constexpr float sUpscaleLevelBias = -1.0f;
```

- `sUpscalerBuilt` goes. So does `RtxSettings::playedIn`: the launcher and the settings window show
  the upscale choice wherever the ray tracer is chosen.
- `Reconstruction` gains `mJitterPhases` (nought where nothing upscales: the sequence runs on, as
  now). `resolve` sets it from the extents. `levelBiasOf` adds `sUpscaleLevelBias`.
- `sampleFrame` jitters by `haltonJitter(frame % phases)` where phases is not nought.
- `Upscale::Native` is FSR at 1:1: jitter on, and FSR as the anti-aliasing. `off` stays for tests and
  references.

### 6.2 The backend (`components/rtxvulkan/upscale/`)

- `Upscaler` becomes one concrete class, the FSR port. `makeUpscaler` goes, and
  `VulkanRenderer::startUpscaler` constructs it. `renderSizeFor` goes: `createTargets` asks
  `renderExtentFor`. The comment about "a quarter of a second" goes with the runtime it described.
- `device/upscalerextensions.hpp` goes, with its three callers' uses and `listMissingUpscalerExtensions`.
- `describeUpscaling` answers `"FSR 3.1.4"`.
- `UpscaleInputs` gains the camera (`Shaders::Camera` of both eyes, for the depth callback) and
  loses nothing. The jitter, the delta and the reset are there already.

### 6.3 FSR 3.1.4 in the backend

**The source.** FidelityFX SDK tag `v1.1.4`, vendored whole and unmodified under
`extern/fidelityfx/`: `sdk/LICENSE.txt` as `LICENSE.txt`, a `README.md` that names the tag, the
commit and the files, and the include closure of the FSR headers — `gpu/ffx_common_types.h`,
`ffx_core.h`, `ffx_core_glsl.h`, `ffx_core_gpu_common.h`, `ffx_core_gpu_common_half.h`,
`ffx_core_portability.h`, `gpu/fsr3upscaler/*.h` but the HLSL callbacks, and what those include
(`glslc -M` lists the closure; SPD and FSR1's RCAS are in it). The vendored files are data: the tree's
format check and include rules do not reach `extern/`.

**The tree's own files**, each derived from an SDK file and carrying its MIT notice:

| File | Derived from |
|---|---|
| `components/rtxvulkan/shaders/upscale/fsrcallbacks.glsl` | `ffx_fsr3upscaler_callbacks_glsl.h`: the bindings in the tree's set, and the loads below |
| `components/rtxvulkan/shaders/upscale/fsr*.comp` (one per pass) | `sdk/src/backends/vk/shaders/fsr3upscaler/*_pass.glsl` |
| `components/rtxvulkan/upscale/fsrconstants.{hpp,cpp}` | the constant blocks and the Lanczos table of `ffx_fsr3upscaler.cpp` |
| `components/rtxvulkan/upscale/upscaler.{hpp,cpp}` | the resource list and the dispatches of `ffx_fsr3upscaler.cpp` |

**The passes**: prepare inputs, luma pyramid, shading change pyramid, shading change, prepare
reactivity, luma instability, accumulate, RCAS. The debug view and the reactive generation are not
ported. Every module is compiled with `FFX_GPU`, `FFX_GLSL`, `FFX_HALF=0`, the SDK's permutation
defines for this input (HDR colour, render-size motion vectors, inverted infinite depth, no jittered
motion vectors), through `glslc`, `openmw-rtx-spirv-pin` and `spirv-val`, like every other module.
The luma pyramid's SPD is compiled without wave operations (F9).

**The inputs**, through the callbacks:

- **Colour**: `TraceResult::mColour`, the composed frame in linear radiance.
- **Depth**: `LoadInputDepth(p)` reads the distance `d` in `CHANNEL_SURFACE.g`, the pixel's ray
  through the eye `CHANNEL_PUFFS` names (its sign, `packPuffs`), and returns
  `sFsrNear / (d * dot(ray, forward))`, with `DEPTH_INVERTED | DEPTH_INFINITE`, and nought where
  nothing was hit. `cameraNear = sFsrNear` (one unit, named with its reason),
  `viewSpaceToMetersFactor = 1 / UNITS_PER_METRE`.
- **Motion**: `LoadInputMotionVector(p)` returns `(motion + jitter) / renderSize`: the channel is
  measured from the jittered sample to last frame's unjittered screen (`accumulate.comp` adds the
  jitter back for the same reason), and FSR wants the vector between the unjittered centres, in UV.
  `motionVectorScale = (1, 1)`. The sign is proved by a test, not assumed (section 8).
- **Exposure**: `FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE`, `preExposure = 1`. FSR's exposure steers its
  own locks only. The picture's exposure stays the display chain's.
- **Reactive and composition masks**: none at first. The puffs are drawn after FSR, so they need
  none. Water and fog may. That is a morning question, because only a moving picture shows it.
- **Sharpening**: off.
- **Output**: an `rgba16f` image at the output extent, which `getOutput` returns to the display.

### 6.4 Selection and settings

- `[RTX] upscale`: `off`, `ultraperformance`, `performance`, `balanced`, `quality`, `native`. The menus
  offer all but `off`, as `sUpscaleMenu` says now.
- `settings-default.cfg`: `upscale = native` (D8). The comment says what native is.
- `RtxRenderer::setUpscale` keeps writing back a mode the renderer refused. FSR refuses nothing a
  device that runs the renderer can run, so that path stays for completeness only.

## 7. Design: running on each vendor

| Required | NVIDIA | AMD Windows | RADV | Intel Windows | ANV |
|---|---|---|---|---|---|
| ray tracing pipeline, ray query, maintenance 1 | yes | yes | gfx10.3+ (RDNA 2+) | Arc | gfx12.5+ (Arc) |
| `VK_KHR_ray_tracing_position_fetch` | yes | 23.7.1+, RX 6000+ | gfx10.3+ | to verify | yes |
| `VK_KHR_shader_fma` | 595+ | 26.3.1+ | Mesa 26.2+ | to verify | yes |
| now | runs | runs | runs | runs if fma | runs |

- Driver floors per `VkDriverId`: NVIDIA 595, AMD proprietary 26.3.1, RADV Mesa 26.2. Intel's come in
  phase 13. The refusal names the floor of the driver it found.
- Breadcrumbs: `VK_AMD_buffer_marker` beside the NVIDIA checkpoints.
- Harness: an amdgpu card source (sysfs) beside NVML, and the Mesa cache variables beside NVIDIA's.
- RDNA 2 and RDNA 3 traverse the BVH in shader code, so path tracing is much slower than on RTX.
  RDNA 4 is the first AMD generation with strong ray tracing hardware.

## 8. Tests

Each test states its expected values and how they were derived. The GPU tests extend
`apps/components_tests/rtxvulkan/trace/visibility/` (`RtxVisibilityTest`, `shoot`, `readChannel`).

1. **The noise measure** (done): `omw noise` over the `noise` suite. Each phase that moves the picture
   reports both places' frame mean and p99 before and after.
2. **The unfiltered frame does not move.** A phase that splits a term out of `direct` changes where
   the term is added, not its value. `omw shot --views=all --map --upscale=off --filter=0 --against`
   must show a worst channel of at most 1 level at every view. Only the filtered frames may move.
3. **The shadow denoiser**, in a new `shadow.cpp` beside `filter.cpp`:
   - A floor fully in the sun and a floor fully under a roof: the filtered `shadow` is exactly 1 and
     exactly 0 at every receiver, and the classification skipped every tile (a count the pass exposes
     to tests, like the digest's lanes).
   - A straight shadow edge under a sun of radius `SUN_SHADOW_RADIUS`: the filtered frame's mean over
     the penumbra is within the same tolerance of the raw mean that `filter.cpp` uses, and its spread
     falls below a fifth of the raw one.
   - A room (no sky): no receiver, `sunlit.rgb` is nought at every pixel, the shadow pass skips every
     tile, and the composite adds nothing.
   - The sum of one unfiltered frame is that frame, extended to the new channel (the existing
     assertion in `theFilterTakesTheNoiseOff...`).
4. **The lamps in the diffuse signal**: a floor under one lamp: the filtered mean stays within the
   `filter.cpp` tolerance of the raw mean and the spread falls, as the sky test asserts for the bounce.
5. **The fast history**: one lamp for 32 frames, then none. The plain exponential blend leaves
   `(1 - 1/16)^k` of the old light after `k` frames. The test asserts that after 8 frames the lit
   floor is within 10% of the dark floor's value, which the plain blend (0.597 of the light left)
   cannot pass.
6. **FSR**, in `apps/components_tests/rtxvulkan/upscale/upscaler.cpp` and the core tests:
   - The core's table, hand computed for 1920×1080: quality 1280×720 and 18 phases; balanced
     1129×635 and 23 (`1920 / 1.7 = 1129.41`, `(1920 / 1129)² × 8 = 23.14`); performance 960×540 and 32;
     ultra performance 640×360 and 72; native 1920×1080 and 8. The level bias at quality is
     `log2(1280 / 1920) - 1 = -1.585`.
   - A flat frame: every pixel one colour, still camera, native and quality. After the phase count's
     frames the output is that colour within a half's step.
   - The motion sign: a still place traced with jitter, FSR over the phase count, against a
     256-frame unjittered reference: the error with the callback's sign is lower than with the sign
     flipped, by more than the reference's own spread.
   - Determinism: two renderers draw the same 24 upscaled frames and agree byte for byte.
7. **The driver floors**: `profileOf` on fixtures for RADV (navi21, Mesa 26.2 and 26.1), AMD
   proprietary (26.3.1 and 26.2.1), and the existing NVIDIA ones. The refusal names the floor of the
   driver found.
8. **ReSTIR** (only with 5b): with one lamp, the temporal estimate equals the direct one frame for
   frame; with a lamp removed, the reservoir never names it again (the row map).

## 9. Licences

The fork is GPLv3. Every third-party part a shipped binary contains must be compatible with it.

| Part | Licence | Compatible with GPLv3 |
|---|---|---|
| FSR 3.1 upscaler (SDK v1.1.4) | MIT | yes; keep the notice |
| FidelityFX Shadow Denoiser (SDK v1.1.4), ported | MIT | yes; keep the notice in each derived file |
| Quake II RTX `asvgf.glsl` | GPL-2.0-or-later | yes, under the "or later" |
| kajiya, AMD Capsaicin (reference reading) | MIT, Apache-2.0 | yes |
| Vulkan Memory Allocator | MIT | yes |
| Crashpad | Apache-2.0 | yes (not with GPLv2) |
| Mesa drm-shim | MIT | a test tool, not shipped |
| FSR 4, FSR Ray Regeneration (SDK 2.x signed DLLs) | AMD binary licence: no reverse engineering | **no** |
| NRD, RTXDI, DLSS | NVIDIA RTX SDKs licence | **no** |
| XeSS | Intel licence, binaries only | **no** |

The GPLv3 requires that a distributed program and every part of the combined work come with
Corresponding Source under the GPLv3 (sections 5 and 6). The NVIDIA RTX SDKs licence, section 2(e),
forbids use "in any manner that would cause it to become subject to an open source software
license". AMD's and Intel's binaries forbid the modification and the source access the GPLv3
requires. This is a reading of the licences, not legal advice.

Algorithms are not covered by these licences: ReSTIR, SVGF, A-SVGF, ReBLUR and ReLAX may be written
in-tree from the papers. NRD's source is not read for that purpose.

## 10. The unattended run

A session carries out section 11 without a person. These rules hold for the whole run, beside
`AGENTS.md` and the user's own rules.

**State.**

- `.notes/denoise-progress.md` is the run's record. Create it at the start. After each step, write
  what was done, the figures, each decision the plan left open and how it was decided, and each
  question for the morning. After a context compaction, read this file and this plan before anything
  else.
- The start: `git status` must be clean. Record the head commit.

**Snapshots.** Committing needs the user's word. If the user said "commit each phase" when they
started the run, commit each green phase on a branch `denoise-night`, one commit per phase, with the
repository's commit style and no other trailer. If not, after each green phase write the whole
change since the start into a patch — `git add -A && git diff --cached --binary > <scratchpad>/phase-N.patch
&& git reset -q` — and record its path. No commit, no stash, no tag.

**Order.** 4, 5a, 6a, 7, 8, 10, 3, 11, 12, then 5b if its condition holds. The value first: the
pier's shadows, the lamps, the anti-aliasing. Section 11 says which phases depend on which.

**Each phase, in this order.**

1. Read the files the phase names, and what calls them.
2. Baselines, before any edit: `./omw shot --views=all --map --upscale=off --out=<scratchpad>/pN-before`
   (and `--filter=0` a second time where test 2 applies), `./omw kernels > <scratchpad>/pN-kernels.txt`
   for a shader phase, and `./omw release noise` for a phase that moves the picture.
3. Build the change. Build the touched targets. Run the covering test binary with a filter.
   `./omw format`.
4. The phase's tests (section 8), then `./omw test`.
5. The phase's proof: `shot --against`, `kernels --against`, `repeat --pairs=10` where a frame reads
   what changed, `noise` where the picture moves.
6. `./omw release bench` for the cost, in the background, after the builds are done (below).
7. `./omw gate`, alone.
8. Record, then snapshot.

**Measuring.** Never a bench or a gate beside a build or another gate. Start a bench in the
background and wait for it without other work. The run's own spinner is on the screen all night, so
every figure is marked "taken under the session" in the record, with the report's `card` lines. A
figure a decision rests on is named in the morning list, to be taken again on a quiet desktop.

**Stop rules.**

- A phase whose proof fails is fixed inside the phase's design. If two attempts at the design level
  do not fix it, the phase stops: record the failure, the figures and the diff in a patch, return the
  tree to the last snapshot (`git checkout -- .` and remove only the phase's own new files, by name),
  and go to the next phase that does not depend on it.
- A phase that makes a place noisier (mean or p99, by more than the two references' own spread, 5
  levels at p99) or the trace slower beyond the bench's run-to-run spread without a noise gain that
  pays for it, stops by the same rule.
- Nothing is installed. Nothing is posted. `view` and `game` are not opened.
- The pinning is never weakened. An operation it refuses in a ported module is rewritten in the port,
  and the record says what and why.
- A bug found outside the phase goes to `.notes/ISSUES.md`, and the run carries on.

**The morning report.** At the end, the record's first section lists: each phase and its state, the
noise table before and after, the cost table, the patches or the commits, the questions, and the
figures to take again.

## 11. Phases

### Phase 4: the shadow denoiser

Depends on nothing. Design: 5.1.

1. `lightPassing`, `skyPassing` and the underwater twin; `lightThrough` and `skyVisible` defined
   through them, so their callers compute what they computed.
2. `DirectLight::mSky`, `mSkySeen` and `gather`'s `split`; `shadeSolid`, `answerSolid`,
   `answerWater`; the payload's `mSunlit` and `ANSWER_SKY_SEEN`.
3. `CHANNEL_SUNLIT` through `gbuffer.h`, `frameimage.hpp`, `bindings.glsl`, `GBuffer`, the digest and
   `readChannel`; the launch's store and its composed path.
   **Checkpoint**: test 2 passes and `repeat --pairs=10` agrees. The picture has not moved yet.
4. `lib/history.glsl` with `sameSurface` taken out of `accumulate.comp`. **Checkpoint**: `kernels
   --against` names no kernel but `accumulate`, and every filtered view is identical by hash.
5. `shadow.h`, `lib/shadowdenoise.glsl` and the three shaders; `ShadowPass`, `ShadowHistory`;
   `TracePasses` and `TraceChain`; the turn moved to `recordDenoise`; the composite's two bindings;
   the CMake lists (the shader list and the backend's file list, alphabetical).
6. Test 3, then the proof: test 2 still holds, `noise` at both places, the bench.

Expected: the pier's frame falls most of the way to its bar. The guild does not move: a room has no
receiver.

### Phase 5a: the lamps join the diffuse signal

Depends on phase 4 only for the `split` parameter; without phase 4 the parameter is added here.
Design: 5.2.

1. `DirectLight::mLamps`, set under `split`; `shadeSolid` adds it to `bounce`.
2. Test 4 and test 2, then `noise` at both places and the bench.

Expected: the guild's frame falls toward its bar. The pier's lamps are few, so it barely moves. If a
lamp's shadow edge comes out blurred, the noise measure's p99 at the guild rises. That is the stop
rule's case, and the record says so.

### Phase 6a: the fast history

Depends on nothing. Design: 5.3.

1. `ACCUMULATE_FAST`, the history pair in `AccumulateHistory` (its `Turn` gains the two images), the
   bindings in `accumulate.h`, the constants in `look.h`, the clamp in `accumulate.comp`.
2. Test 5, then `noise` (a still place must not get noisier: the clamp must not fire on noise alone)
   and the bench.

### Phase 7: the upscaler's sizes in the core

Depends on nothing. Design: 6.1 and the removals of 6.2 that do not need FSR.

1. `upscaleRatio`, `renderExtentFor`, `jitterPhasesFor`, `sUpscaleLevelBias`;
   `Reconstruction::mJitterPhases`; `levelBiasOf`; `sampleFrame`.
2. Remove `upscalerextensions.hpp` and its uses, and `Upscaler::renderSizeFor`: `createTargets` asks
   the core. `makeUpscaler` still refuses, until phase 8.
3. The core's table (test 6, first item) in `apps/components_tests/rtx/frame/`, and the
   reconstruction tests extended with the phases and the bias.
4. Proof: the build, the tests, `shot --against` identical (nothing upscales yet), the gate.

### Phase 8: FSR 3.1.4

Depends on phase 7. Design: 6.2 to 6.4.

1. Vendor `extern/fidelityfx/` (D4 covers the files; the download is read-only from the tag). Record
   the commit hash of the tag in its `README.md`.
2. The shader modules and the callbacks. Build them alone first: each must compile, pin and validate.
3. `FsrConstants` and `Upscaler`: the resources (sizes and formats from the SDK's resource list), the
   Lanczos table, the constant blocks, the dispatch sizes, the reset, the frame index.
4. `VulkanRenderer`: construct the upscaler, `describeUpscaling`, `sUpscalerBuilt` and `playedIn`
   removed, the launcher and the settings window without the build test.
5. `noise`: `Schedule::mUpscale`, the reference and the bar at `off`, the frame at the run's mode
   (F12). The warm-up must cover the phase count: check `--warmup`'s frames against
   `jitterPhasesFor`, and raise the warm-up of `noise` where it is short.
6. `settings-default.cfg`: `upscale = native`. Its comment, and `rtx.rst`'s.
7. Test 6, then the proof: `repeat --pairs=10` (upscaler off by design, so it holds the trace),
   `shot --views=all --upscale=off --against` identical, `shot --views=all --upscale=native --out`
   for the morning to look at, `noise` with `--upscale=native` and `--upscale=quality`, the bench at
   `native` and `quality`.

Expected: `noise` at `native` is lower than the unupscaled frame at every place, because FSR
accumulates across frames. At `quality` it is a new number; the record gives it without a verdict.

### Phase 10: the rates and the levels

Depends on 4, 5a, 6a and 8.

1. For each of `INDIRECT_LIGHT_RATE`, `BOUNCE_RATE`, `AMBIENT_EXTERIOR_RATE` (each 0.5 now): trace at
   0.25, 0.5 and 1.0, with `noise` and the bench at each. Keep the cheapest value whose noise at both
   places is within the references' spread of the best value's.
2. The wavelet's levels under FSR native: 5, 4 and 3, the same way.
3. The two open issues in `.notes/ISSUES.md` about the indirect rate and `STAR_RADIANCE`: judge them
   under the new denoiser; fix each, or record why not, and delete a fixed one from the file.
4. A change of a constant is a change of its comment's reason. The record keeps the table.

### Phase 3: the driver floors

Depends on nothing.

1. `RequiredExtension::mNvidiaDriver` becomes a span of `DriverFloor { VkDriverId mDriver;
   std::string_view mRelease; }`. `VK_KHR_shader_fma`: NVIDIA `595`, AMD proprietary `26.3.1`, RADV
   `Mesa 26.2`. The refusal: "(AMD driver 26.3.1 or later; this one is …)", in the style of the
   NVIDIA line.
2. Fixtures in `apps/components_tests/rtxvulkan/device/physicaldevice.cpp`, with the extension lists
   and properties from vulkan.gpuinfo.org reports of those drivers. Each fixture's comment names its
   report.
3. The drm-shim (D5), built and never installed. In `~/Projects/mesa`, at `mesa-26.2.3` (the
   installed RADV's tag; check `pacman -Q vulkan-radeon` again first):
   `meson setup build-shim -Dbuildtype=release -Dgallium-drivers= -Dvulkan-drivers=amd -Dtools=drm-shim`,
   then `ninja -C build-shim`. No `ninja install`. The run needs
   `LD_PRELOAD=<mesa>/build-shim/src/amd/drm-shim/libamdgpu_noop_drm_shim.so`, `AMDGPU_GPU_ID` set to
   the chip, and `VK_ICD_FILENAMES` set to the build's own RADV ICD JSON. Read the shim's source for
   the exact spelling of the chip names and for what it fakes, before the first run.
4. Under the shim, for navi21 (RDNA 2), navi31 (RDNA 3) and gfx1201 (RDNA 4): `./omw exec
   ./openmw-rtxtool info`. The device must be accepted, or refused with a reason the record then
   explains.
5. Every pipeline compiled for each chip: a harness verb `compile` that builds every kernel tuple
   (`./omw kernels` lists them) on the device and prints, per pipeline, the registers, the spills and
   the scratch size from `VK_KHR_pipeline_executable_properties`. It runs on the real card as well, so
   the NVIDIA figures stand beside the AMD ones. The verb goes through `sVerbs`, `VerbPolicy`,
   `tools/omw/main.py` and `AGENTS.md`'s verb line, as `noise` did.
6. Stop rule of its own: if the shim cannot stand a device up (the renderer needs more of the device
   than a noop shim fakes), record where it stopped and keep items 1 and 2.

### Phase 11: parity of the optional features

Depends on nothing.

1. `DeviceOption::BufferMarkers` for `VK_AMD_buffer_marker`: `GpuTimer::open` writes the zone's index
   by `vkCmdWriteBufferMarkerAMD`, and `Device::describeFault` reads the last one back, as it reads the
   checkpoints. The option test extended.
2. `apps/rtxtool/instruments/amdgpu.{hpp,cpp}`: the busy share, the clocks, the power and the memory
   from `/sys/class/drm/card*/device/`. The parsers are pure functions over text and are tested on
   fixtures. `CardWatch` chooses NVML or amdgpu by the device's PCI vendor.
3. `drivercache.cpp`: `MESA_SHADER_CACHE_DIR`, `MESA_SHADER_CACHE_DISABLE` and
   `MESA_SHADER_CACHE_MAX_SIZE` beside NVIDIA's.

### Phase 12: the documents

`AGENTS.md` (target hardware; `shot` baselines with `--upscale=off`), `README.md`,
`docs/rtx/architecture.md` (the denoiser's passes, the upscaler, the frame's order), `rtx.rst`, the
settings text and its translations. `RtxSourceTreeTest`'s document paths hold. Intel is not named as
supported (D9).

### Phase 5b: ReSTIR DI temporal (conditional)

Only if, after phases 4, 5a and 6a, the guild's frame still fails its bar at p99. Design: 5.4.
Test 8, then `noise` and the bench. Kept only if the guild's p99 falls by more than the references'
spread and the pier does not rise.

### Not in the unattended run

- **6b, A-SVGF temporal gradients** (F11): a design of its own.
- **9, the glossy filter**: needs a PBR replacer on this machine (F8).
- **The FSR reactive mask** for water and fog: a person has to look at a moving picture.
- **13, Intel**: after AMD is done (D9).

## 12. Decisions

Settled:

- **D1. Target:** NVIDIA RTX 20+, AMD RDNA 2+ on Windows and Linux; Intel Arc after AMD (D9).
- **D2. Licences:** NRD, XeSS, DLSS, FSR 4 and Ray Regeneration are out (section 9).
- **D3. Settings:** settings the renderer no longer has are not read; `[RTX] upscale` is the one
  upscaler's quality.
- **D4. FSR:** a port into the backend, from the SDK v1.1.4 source.
- **D5. drm-shim:** built from Mesa in `~/Projects/mesa`, not installed.
- **D6. SER:** removed.
- **D7. The shadow denoiser:** a port of AMD's FidelityFX Shadow Denoiser (section 5.1).
- **D8. Default AA:** `native` FSR for played sessions (section 6.4).
- **D9. Intel:** only after AMD is done, in phase 13. Until then the documents do not name it.

Made by this plan from the codebase (the user can overrule each):

- **D10. No reconstructor registry.** One upscaler with no extensions needs no registry, no traits
  and no virtual seam (F5).
- **D11. The sunlit channel keeps today's estimator**, with the bit set to one, so the unfiltered
  frame does not move (F2, F3).
- **D12. No subgroup operations in the ports** (F1, F9).
- **D13. The lamps join the diffuse signal before any ReSTIR work**, and ReSTIR comes only on the
  measure (5.4).
- **D14. The harness keeps upscaling at `quality` by default** once FSR exists, as `run.hpp` already
  says; baselines name `--upscale=off` (F7).

## Sources

- FidelityFX SDK v1.1.4 (FSR 3.1.4 and the Shadow Denoiser, Vulkan GLSL passes, MIT):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4 and
  https://gpuopen.com/manuals/fidelityfx_sdk/
- AMD FidelityFX Denoiser (shadows, reflections; MIT): https://gpuopen.com/fidelityfx-denoiser/ and
  https://github.com/GPUOpen-Effects/FidelityFX-Denoiser
- FSR SDK 2.x licence (signed DLLs vs MIT `.hlsl`): https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/license.md
- FSR 3.1.5 manual (inputs, jitter, mip bias): https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/
- FSR SDK 2.3 (June 2026): https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/
- FSR Ray Regeneration: https://gpuopen.com/amd-fsr-rayregeneration/
- Quake II RTX (A-SVGF; GPL-2.0-or-later headers): https://github.com/NVIDIA/Q2RTX and
  https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/asvgf.glsl
- kajiya GI overview (FFX shadow denoiser, ReSTIR GI, costs on RX 6800 XT): https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md
- AMD Capsaicin (MIT): https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin
- ReLAX and ReBLUR (Ray Tracing Gems II): https://www.researchgate.net/publication/354065087_ReBLUR_A_Hierarchical_Recurrent_Denoiser
- A Gentle Introduction to ReSTIR (SIGGRAPH 2023 course): https://intro-to-restir.cwyman.org/
- Denoising overview: https://alain.xyz/blog/ray-tracing-denoising
- NRD and its licence: https://github.com/NVIDIA-RTX/NRD
- RTXDI licence: https://github.com/NVIDIA-RTX/RTXDI
- XeSS SDK: https://github.com/intel/xess
- `VK_EXT_ray_tracing_invocation_reorder` (Khronos, November 2025): https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder
- Mesa feature list: https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/features.txt
- AMD Windows Vulkan driver history: https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-VULKAN.html
- Vulkan Hardware Database (device reports for the fixtures): https://vulkan.gpuinfo.org/
- Mesa AMD drm-shim: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/amd/drm-shim
- GPLv3: https://www.gnu.org/licenses/gpl-3.0.html
