# RTX renderer audit: memory traffic, allocation and channel count

Five read-only audits of the ray tracer, each against published practice: the G-buffer and the trace's
payload, the denoisers, the ray tracing core and the scene on the device, memory and data movement,
and every other pass (air, water, sky, sprites, upscaler, display). Their record, with every file:line
and every source, is in `.notes/rtx-audit/{gbuffer,denoise,raytracing,memory,passes}.md`.

What was worth doing is done; the rest is declined below, against what the benches measured. What
stays open is group 12: each item is a policy or a change to the vanilla picture, and so a decision.

Sizes: B/px is bytes per traced pixel. 1 B/px is 1.64 MB at 1707×960 (`quality` at 2560×1440 shown) and
3.69 MB at 2560×1440 traced.

## Where the memory and the time are

| Owner | Today | Published practice |
|---|---|---|
| `DenoiseHistory` | **208 B/px** (341 MB at 1707×960) | NRD RELAX diffuse+specular 81 B/px, SIGMA shadow 15 B/px |
| G-buffer: 19 channels | 138 B/px (226 MB); 19 B/px of it written and never read | an NRD game's G-buffer ~40–60 B/px |
| Fog volume: one froxel per 12×12 pixels a slice, 0.44 per traced pixel | ~100 B/froxel (74 MB) | Frostbite and UE ~40 B/froxel; RTX Remix a grid 5× coarser |
| FSR at render size | ~40 B/px | matches the SDK exactly; AMD lists 106 MB at 1440p Quality |
| **Total per traced pixel** | **~437 B/px** | |

- On the GPU, the frame at `seyda-neen-ship` is 6.8 ms: trace 2.78, the denoisers 1.5 (21–31 % of the
  frame across the places), air 0.41, TLAS 0.41, upscale 0.37.
- Descriptor limits are not a risk: the most a stage binds is 22 storage images, and the widest pushed
  set is 21 against `maxPushDescriptors` = 32 on NVIDIA and RADV.

Every history the denoisers blend is now held at its shader's own rounding: halves and sixteen-bit
unorms rounded at random where a blend reads them back (`lib/halfround.glsl`, `lib/shadowword.glsl`),
proven by `RtxHalfStoreTest` and `RtxShadowWordTest`.

## Declined

Not proposed again: opacity micromaps, async compute, SER (AGENTS.md's declined list), TLAS update or a
split TLAS (against NVIDIA's guidance and the worst-frame rule), R11G11B10F radiance, RGB10A2 normals
(same size, coarser), a visibility buffer, fusing the accumulator with its clamp, a shared-memory tile
in the air's integrate pass (measured 16–29% slower at scale 12: two barriers a slice, against fetches
the texture cache already served), and BLAS builds in fixed-size chunks (a whole load's build scratch
measured 4.3 MiB at `seyda-neen-ship`, its staged positions 1.2 MiB).

And the audit's other groups, for returns too small against what the benches measured. The shadow
word showed the trade: 48 B/px saved cost the shadow zone 0.014–0.019 ms at 1280×720 traced, where the
images sat in the cache and the packing's arithmetic outweighed the traffic it saved.

- **Measurement first** (group 0): counters and ceilings for groups 6–8, which are declined with them;
  the `[vkrt] Analysis` driver thread is the GPU-bound frame's and not its limit.
- **G-buffer exact packing** (group 2): 4–8 B/px.
- **The rest of the stochastic-rounding halves** (group 4): the held surfaces to the G-buffer, the
  bounce and fill in one texel, the fog history pairs, the payload radiances and the bloom pyramid —
  memory, or time only where a profile says the fetches limit.
- **Transient memory by aliasing** (group 5): memory only, and it only pays where memory is the
  constraint.
- **Scene tables and structures** (group 6): 20–50 MB.
- **TLAS and traversal** (group 7): each item after a ceiling nobody measured.
- **The air volume's scan** (group 8): an estimated 0.02–0.04 of a 0.07 ms zone.
- **Water** (group 9): one SPD dispatch for the mip chains, an estimated 0.03–0.06 ms of 0.24; the wake
  ring, 4 MB.
- **Display and sprites** (group 10): one sprite bin set, which a storm alone fills; the histogram's
  read of the shown frame.
- **Denoiser dispatch shape** (group 11): the pane pass it would skip measured 0.036–0.049 ms.

## Group 12: decisions that are yours

Each changes a policy or the vanilla picture, so none is a fix to make without your call.

- **The glossy filter's images exist on content that can never have a lobe** (24 B/px). Make them only
  where the content can name a companion map, decided at construction.
- **Ground composites by the footprint the trace reads** (now BC7, 148 MB on `island-crossing`): a
  composite level no ray reads changes no pixel when it is not made. Count the finest level the
  trace reads first; at 256² every composite is a quarter of its size.
- **Groundcover met by fewer ray types** (lamp shadow, bounce far hit, fog lamp rays), as upstream never
  shadows grass. Needs an instance-mask bit freed; changes the look.
- **Octahedral vertex normals** (~20 MB): pictures move at the 1e-5 level.
- **The exterior's 145 MB of structures and 150 MB of tables stay resident inside interiors**, for a fast
  return. Keep, keep a band, or release.

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
