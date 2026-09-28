# Denoising and upscaling on NVIDIA, AMD and Intel

A proposal for the denoiser and the upscaler, so that one renderer gives a clean, stable picture on
NVIDIA RTX 20 and later, AMD RDNA 2 and later, and — only after AMD is done — Intel Arc. Everything
in it is compatible with the GPLv3 and runs through Vulkan. It also removes Shader Execution Reordering (SER),
which is the one requirement that stops the renderer on Mesa's drivers.

The recommendation, in short:

1. **Remove SER.** The trace uses hit objects for structure only, and sorting lost every time it was
   measured. Without it, RADV (AMD on Linux) can run the renderer, and later ANV (Intel on Linux).
2. **Denoise by signal, the way shipped path tracers do.** The sun's and the moons' shadows go
   through a dedicated shadow denoiser (a port of AMD's FidelityFX Shadow Denoiser, MIT). The lamps are
   sampled with ReSTIR before any filter sees them. The lamps' diffuse light joins the bounce in one
   demodulated diffuse signal that the existing accumulator and wavelet filter. Glossy light gets a
   temporal filter only.
3. **Upscale and anti-alias with FSR 3.1**, ported into the backend (MIT, analytic, every vendor).
   FSR's native mode is the renderer's TAA.
4. **No vendor ML library.** DLSS, FSR 4, FSR Ray Regeneration, XeSS and NRD all have licences that
   conflict with the GPLv3 (section 9).

All research is from September 2026. The sources are at the end.

## 1. Where the tree stands

Measured on the RTX 4090, release build, 1920×1080, the default bench suite.

| Part | Now | Cost |
|---|---|---|
| Indirect diffuse (the bounce) | `AccumulatePass` (SVGF temporal) + `AtrousPass` (5 levels, 25 taps) over `CHANNEL_INDIRECT` | accumulate 0.25–0.30 ms, wavelet 1.8–2.9 ms |
| Sun and moon shadows | one ray across the disc per pixel (`SUN_SHADOW_RADIUS`), unfiltered | in the trace |
| Lamps | one-deep reservoir per pixel (`Reservoir`), one shadow ray, unfiltered | in the trace |
| Glossy light (PBR replacers) | one lobe draw, joins the direct channel, unfiltered | in the trace |
| Anti-aliasing | none: jitter is off by default, and no pass resolves a history | — |
| Upscaling | none: the `Upscaler` seam refuses every mode but `off` | — |
| SER | required: `VK_EXT_ray_tracing_invocation_reorder` | — |

What that looks like: at the Seyda Neen pier at noon (`view --cell="-2,-9"`), the frame with the wavelet
on and the frame with it off are nearly the same. The wavelet ran (1.37 ms at 1280×720), but the noise
is not in the bounce. Against a 256-frame reference, 11% of the pixels where the sun is partly visible
are more than 30 levels too dark and 7% are more than 30 levels too bright: one sun ray per pixel,
through leaves and at penumbrae. DLSS Ray Reconstruction cleaned all of this until it was removed.

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
- **Split by frequency.** A shadow is an edge; a bounce is smooth. One filter tuned for both blurs the
  edge or leaves the bounce noisy. Q2RTX's HF/LF/SPEC split and NRD's SIGMA beside ReBLUR both say so.
- **A dedicated shadow denoiser for the sun.** It filters one number per pixel, skips tiles that are
  fully lit or fully shadowed, and keeps the lighting exact: direct sun = unshadowed sun × filtered
  visibility. AMD's (MIT) and NVIDIA's SIGMA are both built this way.
- **Reduce variance before you filter.** ReSTIR DI reuses light samples across frames and neighbours.
  A filter cannot recover what a one-sample lamp choice throws away without also blurring the picture.
- **Temporal first, then spatial, with the variance as the guide.** SVGF's structure. Two refinements
  the tree lacks: a fast history clamped to limit ghosting (ReLAX), and temporal gradients that reset
  history where the lighting changed (A-SVGF). Morrowind's torches flicker, so the second one matters.
- **Glossy light: temporal only.** Q2RTX found that spatial filters fail on normal-mapped surfaces.
- **Denoise at render size, before the upscaler; anti-alias after.** A temporal upscaler also
  accumulates, so the denoiser in front of it can be lighter. With no upscaling, the upscaler's native
  mode is the anti-aliasing.
- **Blue noise, rotated over time**, which the tree already has (`Rtx::BlueNoise`).

## 3. What is on offer

### 3.1 Denoisers

| Option | API | Licence | Fit |
|---|---|---|---|
| FidelityFX Shadow Denoiser (SDK 1.1.4, and `GPUOpen-Effects/FidelityFX-Denoiser`) | DX12, Vulkan; HLSL | MIT | **Yes**, for the sun and the moons. kajiya used a modified port. |
| FidelityFX Reflection Denoiser | DX12, Vulkan | MIT | Later, if glossy reflections need a spatial pass. |
| A-SVGF from Quake II RTX (`asvgf.glsl`) | Vulkan, GLSL | GPL-2.0-or-later | **Yes, as source to learn from and to port**: the "or later" makes it GPLv3-compatible. |
| In-tree SVGF (`AccumulatePass`, `AtrousPass`) | this backend | this tree | **Yes**, extended to the combined diffuse signal. |
| ReSTIR DI (papers, SIGGRAPH 2023 course) | — | algorithm, no code | **Yes**, written in-tree. The RTXDI SDK is under the NVIDIA RTX SDKs licence and is not used. |
| NRD (ReBLUR, ReLAX, SIGMA) | DX12, Vulkan | NVIDIA RTX SDKs licence | **No** (section 9). Its published papers may guide; its source may not be copied. |
| FSR Ray Regeneration 1.2 (FSR SDK 2.3, June 2026) | DX12 only, Windows 11, RX 9000 only | signed binaries, restrictive licence | **No**: no Vulkan, one vendor, and the licence. |
| Intel Open Image Denoise | CPU, CUDA, HIP, SYCL | Apache-2.0 | **No**: an offline and interactive denoiser, not one for a frame budget of a few milliseconds, and not Vulkan. |

### 3.2 Upscalers

| Option | API | Licence | Fit |
|---|---|---|---|
| FSR 3.1.4 (FidelityFX SDK 1.1.4) | DX12, Vulkan; GLSL passes for Vulkan | MIT | **Yes.** Analytic, every vendor. |
| FSR 3.1.5 (FSR SDK 2.3) | DX12; HLSL source | MIT for the `.hlsl` files | The fixes after 3.1.4, read from source and merged by hand where they matter. The 2.x SDK has no Vulkan backend. |
| FSR 4.1 (FSR SDK 2.3) | DX12 only, RX 7000/9000 | signed DLLs, "no reverse engineering" licence | **No.** The community Vulkan port (`FireBurn/FSR-Vulkan`) ships no official model. |
| XeSS 2 | DX11, DX12, Vulkan | Intel licence, binaries only | **No** (section 9). |
| DLSS | — | NVIDIA RTX SDKs licence | **No**; removed. |
| Snapdragon GSR 2 | GLES, Vulkan | BSD-3-Clause | No: made for mobile, lower quality than FSR 3.1 on desktop. |

A cross-vendor neural denoiser or upscaler of the tree's own, on `VK_KHR_cooperative_matrix` (NVIDIA
Turing+, RADV RDNA 3+, ANV), is possible later. It needs a trained model, training data and a budget
this plan does not have. It is a research item, not a phase.

## 4. Design: the signals

The trace writes the light in parts, each to the filter made for it. The composite puts them back
together. Water, emission, the sky and the fog stay resolved in the trace, as now.

| Signal | Written by | Filter | Channel |
|---|---|---|---|
| Sun and moon visibility | `skyVisible` | shadow denoiser (4.1) | new: `CHANNEL_SHADOW`, `R8` or `R16F` |
| Unshadowed sun and moon light, demodulated | the trace | none: it is exact | folded into the composite |
| Lamps' diffuse light, demodulated | ReSTIR DI (4.2) | diffuse denoiser (4.3) | joins `CHANNEL_INDIRECT` |
| The bounce, demodulated | the trace | diffuse denoiser (4.3) | `CHANNEL_INDIRECT` |
| Glossy light (lamps, sun, bounce), demodulated by the specular albedo | the trace | temporal only (4.4) | new: `CHANNEL_SPECULAR` |
| Emission, sky, water, fog, panes | the trace | none | `CHANNEL_DIRECT` |

The composite becomes
`direct + albedo × (sunUnshadowed × shadow + diffuseFiltered) + specularAlbedo × specularFiltered`.

`CHANNEL_SPECULAR` and the specular albedo are the channels the DLSS removal took out. They come back
for the tree's own filter, and only where a PBR replacer gives a surface a lobe: a vanilla surface has
none, and writes nought.

### 4.1 The shadow denoiser (sun and moons)

- A port of the FidelityFX Shadow Denoiser into `components/rtxvulkan/trace/denoise/shadow/`,
  source vendored under `extern/` with the MIT notice. Its passes: prepare (pack the visibility into
  bit masks by tile), tile classification (skip tiles with no variance, reproject the history), and a
  small spatial filter run over three levels.
- Inputs it asks for: the visibility, the normal and the depth (the surface channel holds both), the
  motion vectors, and the previous frame's moments. All exist.
- The trace writes one bit of visibility per sun ray. The unshadowed sun term is computed exactly in
  the trace as now, and the composite multiplies it by the filtered visibility. So the texture and the
  normal detail under a penumbra stay sharp.
- The moons: the same pass, the same mask. Only one of the sun or a moon is drawn per pixel
  (`gather`'s draw), so one mask holds both.
- Cost: kajiya measured 0.52 ms for the sun's rays and this filter together at 1080p on an RX 6800 XT.
  Measure here before a number is quoted.

### 4.2 ReSTIR DI for the lamps

- Temporal reuse first: reproject last frame's reservoir by `CHANNEL_MOTION`, combine it with this
  frame's by the rule that built it (`considerLamp`), cap its count (M ≈ 20), trace one shadow ray.
  `Reservoir` in `lights.glsl` is already the record to carry.
- Spatial reuse second, and only if the bench says the temporal step is not enough: a few neighbours
  in a disc, with the normal and depth test the filters use.
- Visibility reuse needs care: a reservoir from last frame is unbiased only if its shadow ray is traced
  again. The SIGGRAPH 2023 course lists the pitfalls.
- The earlier record said a carried reservoir was "not worth building". That measurement was made
  under Ray Reconstruction, which hid the lamp noise. It has to be measured again.

### 4.3 The diffuse denoiser (lamps and bounce)

The existing `AccumulatePass` and `AtrousPass`, over one signal: the lamps' diffuse light plus the
bounce, demodulated by the albedo. This is what NRD's RTXDI integration does with ReLAX. Changes:

1. **Fast history clamping** (ReLAX): keep a short history beside the long one, and clamp the long
   one to the short one's neighbourhood. This limits ghosting when a lamp moves or turns off.
2. **Temporal gradients** (A-SVGF, as in Q2RTX): for a few pixels a frame, trace last frame's sample
   again with this frame's state, and drop history where the answer changed. For flickering torches.
   Port from Q2RTX's `asvgf.glsl`, which is GPL-2.0-or-later.
3. **Fewer levels under an upscaler.** FSR accumulates too, so the wavelet in front of it may need
   three levels, not five. Measure with `omw shot` and `omw bench`.
4. **Half size, measured.** Q2RTX filters its indirect light at 1/3 size and kajiya traces it at 1/2.
   The bounce is low-frequency; the lamps are not. Try half size for the bounce only if the combined
   signal cannot stay at full size in budget.

The trace's own rates (`INDIRECT_LIGHT_RATE`, `mBounceRate`, `AMBIENT_EXTERIOR_RATE`) were judged under
Ray Reconstruction. They are judged again under this denoiser (`.notes/ISSUES.md` lists them).

### 4.4 Glossy light

- Temporal only, as in Q2RTX: reproject by the surface motion, clamp to the neighbourhood, blend.
  Spatial filters smear normal-mapped highlights.
- Only surfaces with a lobe write here, so the pass skips vanilla content by tile.
- A spatial pass (the FidelityFX Reflection Denoiser, MIT) is a later step, only if the bench and the
  pictures show that glossy replacers need it.

### 4.5 Order of a frame

```
trace → shadow denoise → ReSTIR resolve (in the trace) → accumulate → wavelet → specular temporal
      → composite → FSR (upscale, or native AA) → puffs → display chain → GUI
```

The puffs and the display chain already come after the reconstruction (`spritecomposite.rgen`).

## 5. Design: the upscaler

This keeps the earlier plan, with FSR's native mode now also the anti-aliasing.

### 5.1 The core (`components/rtx/frame/`)

```cpp
/// What a reconstructor is, as the frame rule needs to know it.
struct ReconstructorTraits
{
    ReconstructorId mId;              // Fsr, ... (a NamedEnum, one list of spellings)
    JitterSequence mJitter;           // the phase count rule the reconstructor asks for
    float mLevelBiasOffset = 0.0f;    // added to log2(render / output): -1 for FSR
};
```

- `sUpscalerBuilt` goes away. What is built and what is available are run-time answers from the
  backend.
- `Reconstruction::resolve` reads the traits. The texture level bias is
  `log2(render / output) - 1` for FSR, as FSR's guide says.
- `Upscale::Native` is FSR at 1:1: jitter on, and FSR as the anti-aliasing. `off` stays for tests and
  references.

### 5.2 The backend (`components/rtxvulkan/upscale/` becomes `reconstruct/`)

- `Upscaler` becomes `Reconstructor`: `getTraits`, `renderSizeFor`, `resize`, `release`, `getOutput`,
  `record(const ReconstructInputs&)`.
- `ReconstructInputs` grows from `UpscaleInputs`: the near and far planes, the vertical field of view,
  the pre-exposure, and an optional reactive mask. The G-buffer holds the colour and the motion FSR
  asks for. It holds the distance along each ray, not a device depth, so the FSR port derives the depth
  from the distance, the planes and the pixel's ray in its own input pass, on the frames it upscales.
- A registry replaces the link-time choice:

  ```cpp
  struct ReconstructorFactory
  {
      ReconstructorId mId;
      std::span<const char* const> (*mInstanceExtensions)();
      std::span<const char* const> (*mDeviceExtensions)();
      Availability (*mProbe)(const Device&, VkInstance);   // available, or why not
      std::unique_ptr<Reconstructor> (*mMake)(const Device&, VkInstance);
  };
  ```

- `upscale/upscalerextensions.hpp` goes away; its two declarations become the factory's two fields.
- `Renderer` gains `describeReconstructors()`: each built reconstructor, with "available" or the
  reason not. The settings window lists the available ones; the launcher, which has no device, lists
  the built ones.

### 5.3 FSR 3.1 in the backend (`reconstruct/fsr/`)

- Source: FidelityFX SDK v1.1.4 (FSR 3.1.4), `sdk/include/FidelityFX/gpu/fsr3upscaler/*.h` and
  `sdk/src/backends/vk/shaders/fsr3upscaler/*.glsl`, vendored under `extern/fsr3upscaler/` with the MIT
  licence text. Compiled with the tree's `glslc`, pinning and `spirv-val`, as Godot did with FSR 2.2.
  The SDK's own compiler (`FidelityFX_SC`, Windows only) and its runtime are not used.
- FSR 3.1.5's changes are read from the MIT `.hlsl` source in FSR SDK 2.3 and merged by hand where they
  change the picture.
- Passes (9 compute): prepare inputs, luma pyramid, shading change pyramid, shading change, prepare
  reactivity, luma instability, accumulate, RCAS, and the debug view (debug builds only).
- Host: a port of `ffx_fsr3upscaler.cpp`: the constant blocks, the resource sizes, the jitter phase
  count (Halton 2,3; `ffxFsr3UpscalerGetJitterPhaseCount`), and the fixed ratios: 1.5 quality, 1.7
  balanced, 2.0 performance, 3.0 ultra performance, 1.0 native.
- Every FSR module goes through `openmw-rtx-spirv-pin`, so FSR frames are the same on every compile and
  `omw repeat` holds. Compile with `FFX_HALF=0` first.
- The reactive mask: water and fog first, measured with `omw shot`, because both change without a
  motion vector that describes them.

### 5.4 Selection and settings

- `[RTX] upscale` is the quality of the one upscaler: `off`, `ultraperformance`, `performance`,
  `balanced`, `quality`, `native`. A key that chooses between upscalers comes with a second one.
- An upscaler the device cannot run leaves the renderer at `off`, logs why, and writes the setting
  back, as `RtxRenderer::setUpscale` does now.
- The default for a played session becomes `native`: FSR as the anti-aliasing.

## 6. Design: SER removal

### 6.1 Why

- The launch never sorts. `Requirements::mInvocationReorder` records that sorting was measured four
  ways and lost every one: a reorder point cost 17 to 23% of the trace, and the launch is already 89 to
  100% coherent on the sort key. The bounce, the lamp reservoir and the cutout loop diverge, and no key
  names them. Sorting the bounce was measured 20% slower out of doors and 30% in a room
  (`shading.glsl`).
- The hit objects give structure only: the launch reads the hit, the distance and the ray off the hit
  object instead of carrying them.
- The extension shuts out RADV and ANV: Mesa's feature list has no driver with it (September 2026).
  AMD's Windows driver has it from 26.2.1; NVIDIA from 595.

### 6.2 What changes

- `visibility.rgen`: `RTX_TRACE` becomes `traceRayEXT` into the shading payload, and `RTX_SHADE`
  goes away: the closest-hit or miss shader runs inside the trace. `RTX_TRACE_AND_SHADE` becomes
  `RTX_TRACE`.
- The payload gains the hit flag and the hit distance: 13 words. The launch keeps the ray's origin and
  direction in local variables.
- "Trace, and shade only on a hit" (the arms, and the peel through the arms): trace with a miss record
  that shades nothing (`MISS_RECORD_NONE`), so a miss costs no sky. No second traversal.
- The any-hit shader now sees the larger payload. It reads nothing, but traversal pays the register
  pressure the two-payload design avoided. Measure: if it costs, the arms' first ray becomes a ray query
  in the launch.
- Removed: `Reorder`, `sReorderNames`, `RenderProfile::mReorder`, the `REORDER` specialisation
  constant, `textureHintOf`, the harness's `--reorder` and the bench record's field, the required
  extension, feature and properties, the `info` line, `GL_EXT_shader_invocation_reorder` in
  `traceprobe.rgen` (rewritten with `traceRayEXT`), and the Vulkan 1.4.333 floor in `build.cmake`
  (1.4.329 is enough for `VK_KHR_shader_fma`).
- The NVIDIA driver floor stays 595, because of `VK_KHR_shader_fma`.

### 6.3 How it is proved

- `omw kernels` names every kernel that moved; `omw shot --views=all --against` must show every trace
  column identical, because the shaders compute the same values in the same order.
- `omw repeat --pairs=10` identical.
- `omw release bench` before and after, back to back: the trace must not be slower beyond the
  run-to-run spread.

## 7. Design: running on each vendor

| Required | NVIDIA | AMD Windows | RADV | Intel Windows | ANV |
|---|---|---|---|---|---|
| ray tracing pipeline, ray query, maintenance 1 | yes | yes | gfx10.3+ (RDNA 2+) | Arc | gfx12.5+ (Arc) |
| `VK_KHR_ray_tracing_position_fetch` | yes | 23.7.1+, RX 6000+ | gfx10.3+ | to verify | yes |
| `VK_KHR_shader_fma` | 595+ | 26.3.1+ | Mesa 26.2+ | to verify | yes |
| `VK_EXT_ray_tracing_invocation_reorder` | 595+ | 26.2.1+ | no | Arc B-series | no |
| after SER removal | runs | runs | runs | runs if fma | runs |

- Driver floors per `VkDriverId`: NVIDIA 595, AMD proprietary 26.3.1, RADV Mesa 26.2. Intel's come in
  phase 13 (D9). The refusal names the floor of the driver it found.
- Breadcrumbs: `VK_AMD_buffer_marker` beside the NVIDIA checkpoints.
- Harness: an amdgpu `CardSource` (sysfs and `fdinfo`) beside NVML; the Mesa cache variables beside
  NVIDIA's. Intel's card source is not planned.
- Hardware expectations: RDNA 2 and RDNA 3 traverse the BVH in shader code, so path tracing is much
  slower than on RTX; RDNA 4 is the first AMD generation with strong ray tracing hardware. Arc
  Alchemist has hardware traversal; its frame times are unknown here.

## 8. Tests

1. **A noise measure against a reference.** A `check` claim: at fixed places, the frame's error
   against a `--accumulate=256` reference (mean and 99th percentile, in the display's values) stays
   under a threshold. The pier at noon, the guild at night, a flickering torch. This turns "the picture
   is noisy" into a number each phase must move.
2. **The shadow denoiser:** a flat lit plane and a flat shadowed plane come back exact; a straight
   shadow edge stays within one pixel of the reference's edge; tiles with no variance are skipped
   (the classifier's count).
3. **ReSTIR:** with one lamp, the resampled estimate equals the direct one; with many, the mean over
   frames matches the reference within its confidence interval; a lamp that turns off leaves no light
   after the history cap.
4. **FSR:** render sizes per ratio (exact, from the fixed ratios); a flat frame resolves to itself;
   `omw repeat`.
5. **SER removal:** section 6.3.
6. **AMD without AMD hardware:** Mesa's drm-shim (`libamdgpu_noop_drm_shim.so`, built from the Mesa tag
   that matches the installed RADV, with `-Dtools=drm-shim`, not installed) gives RADV's real
   extensions for navi21, navi31 and gfx1201, and compiles every pipeline. A harness verb `compile`
   prints registers and spills per pipeline from `VK_KHR_pipeline_executable_properties`.
7. **Real hardware** for frame times only: a community tester, or a cloud GPU (Azure NGads V620,
   RDNA 2).

## 9. Licences

The fork is GPLv3. Every third-party part a shipped binary contains must be compatible with it.

| Part | Licence | Compatible with GPLv3 |
|---|---|---|
| FSR 3.1 upscaler (SDK 1.1.4; `.hlsl` of SDK 2.3) | MIT | yes; keep the notice |
| FidelityFX Shadow and Reflection Denoisers | MIT | yes; keep the notice |
| Quake II RTX `asvgf.glsl` | GPL-2.0-or-later | yes, under the "or later" |
| kajiya (reference reading) | MIT or Apache-2.0 | yes |
| AMD Capsaicin (reference reading, DX12) | MIT | yes |
| Vulkan Memory Allocator | MIT | yes |
| Crashpad | Apache-2.0 | yes (not with GPLv2) |
| Mesa drm-shim | MIT | a test tool, not shipped |
| FSR 4, FSR Ray Regeneration (SDK 2.x signed DLLs) | AMD binary licence: no reverse engineering | **no** |
| NRD, RTXDI, DLSS | NVIDIA RTX SDKs licence | **no** |
| NVIDIA STBN | NVIDIA source code licence | not needed: the tree has its own blue noise |
| XeSS | Intel licence, binaries only | **no** |

Why the "no" rows conflict: the GPLv3 requires that a distributed program and every part of the
combined work come with Corresponding Source under the GPLv3 (sections 5 and 6), and the system-library
exception does not cover an SDK the program ships. The NVIDIA RTX SDKs licence, section 2(e), forbids
use "in any manner that would cause it to become subject to an open source software license". AMD's and
Intel's binaries forbid the modification and the source access the GPLv3 requires. This is a reading of
the licences, not legal advice, but the texts point the same way.

Algorithms are not covered by these licences: ReSTIR, SVGF, A-SVGF, ReBLUR and ReLAX are published in
papers and may be written in-tree from the papers. NRD's source is not read for that purpose.

## 10. Plan

Each phase ends green on `./omw test` and `./omw gate`. A phase that moves the picture says so, and
measures the move against the noise measure (test 1).

| Phase | Work | Proof | Result |
|---|---|---|---|
| 1 | The noise measure (test 1) and its references. | the claim fails on today's pier | Noise is a number. |
| 2 | SER removal (section 6). | trace columns identical; bench | RADV and ANV can run. |
| 3 | Driver floors per `VkDriverId`; drm-shim fixtures for navi21, navi31, gfx1201. | `profileOf` on the fixtures | AMD runs on Windows and Linux. |
| 4 | Shadow denoiser (4.1) and the shadow channel. | test 2; noise measure at the pier | Clean sun and moon shadows. |
| 5 | ReSTIR DI temporal (4.2); the lamps join the diffuse signal (4.3). | test 3; noise measure in the guild | Clean lamps. |
| 6 | Diffuse denoiser: fast history, then temporal gradients (4.3 items 1 and 2). | torch flicker without ghosting | Stable lamps and bounce. |
| 7 | Reconstructor abstraction and registry (5.1, 5.2). | `resolve` table-driven over traits | Swappable reconstructors. |
| 8 | FSR 3.1 port (5.3); `native` as the default AA (5.4). | test 4; `omw repeat` | Anti-aliasing and upscaling on every vendor. |
| 9 | Glossy temporal filter (4.4) and specular channel. | noise measure on a PBR replacer place | Clean glossy replacers. |
| 10 | Rates judged again under the new denoiser; wavelet levels under FSR (4.3 items 3 and 4). | bench and noise measure | The budget is back. |
| 11 | Breadcrumbs, the amdgpu card source, the Mesa cache variables. | unit tests of each source | Parity of the optional features on AMD. |
| 12 | Documents: `AGENTS.md` target hardware, `README.md`, `architecture.md`, `rtx.rst`, settings text, translations. | `RtxSourceTreeTest` document paths | The tree says what it supports. |
| 13 | Intel, after AMD is done (D9): driver floors for Intel Windows and ANV, the checks marked "to verify" in section 7, an ANV fixture from `vulkaninfo`. | `profileOf` on the fixture | Intel Arc runs. |

Phases 4 to 6 fix what the pier shows and help every vendor, so they come before the upscaler. SER
removal comes first because it is the smallest change that opens RADV. It opens ANV too, but Intel
stays out of scope until phase 13.

## 11. Decisions

Settled:

- **D1. Target:** NVIDIA RTX 20+, AMD RDNA 2+ on Windows and Linux; Intel Arc after AMD (D9).
- **D2. Licences:** NRD, XeSS, DLSS, FSR 4 and Ray Regeneration are out (section 9).
- **D3. Settings:** settings the renderer no longer has are not read; `[RTX] upscale` is the one
  upscaler's quality.
- **D4. FSR:** a port into the backend.
- **D5. drm-shim:** built from Mesa in `~/Projects/mesa`, not installed.
- **D6. SER:** removed.
- **D7. The shadow denoiser:** a port of AMD's FidelityFX Shadow Denoiser (section 4.1).
- **D8. Default AA:** `native` FSR for played sessions (section 5.4).
- **D9. Intel:** only after AMD is done. Intel's driver floors and its tests are the last phase
  (13). Until then Intel is not tested, and the documents do not name it as supported.

## Sources

- AMD FidelityFX Denoiser (shadows, reflections; MIT): https://gpuopen.com/fidelityfx-denoiser/ and
  https://github.com/GPUOpen-Effects/FidelityFX-Denoiser
- FidelityFX SDK (FSR 3.1.4, Vulkan, MIT): https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK and
  https://gpuopen.com/manuals/fidelityfx_sdk/
- FSR SDK 2.x licence (signed DLLs vs MIT `.hlsl`): https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/license.md
- FSR 3.1.5 manual (inputs, jitter, mip bias): https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/
- FSR SDK 2.3 (June 2026; FSR 4.1.1 on RDNA 3 and 4, Ray Regeneration 1.2, DX12): https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/
- FSR Ray Regeneration: https://gpuopen.com/amd-fsr-rayregeneration/
- Community Vulkan FSR providers (not official): https://github.com/FireBurn/FSR-Vulkan
- Quake II RTX (A-SVGF; GPL-2.0-or-later headers): https://github.com/NVIDIA/Q2RTX and
  https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/asvgf.glsl
- kajiya GI overview (FFX shadow denoiser, ReSTIR GI, costs on RX 6800 XT): https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md
- AMD Capsaicin (MIT): https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin
- ReBLUR (Ray Tracing Gems II): https://www.researchgate.net/publication/354065087_ReBLUR_A_Hierarchical_Recurrent_Denoiser
- A Gentle Introduction to ReSTIR (SIGGRAPH 2023 course): https://intro-to-restir.cwyman.org/
- Denoising overview: https://alain.xyz/blog/ray-tracing-denoising
- NRD and its licence: https://github.com/NVIDIA-RTX/NRD
- RTXDI licence: https://github.com/NVIDIA-RTX/RTXDI
- STBN licence: https://github.com/NVIDIA-RTX/STBN
- XeSS SDK: https://github.com/intel/xess
- `VK_EXT_ray_tracing_invocation_reorder` (Khronos, November 2025; vendor support):
  https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder
- Mesa feature list: https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/features.txt
- AMD Windows Vulkan driver history: https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-VULKAN.html
- Mesa AMD drm-shim: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/amd/drm-shim
- GPLv3: https://www.gnu.org/licenses/gpl-3.0.html
