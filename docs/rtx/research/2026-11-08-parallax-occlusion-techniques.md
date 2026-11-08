# Parallax occlusion mapping: what the field does — 2026-11-08

A survey of published methods and of one shipping path tracer, read against what the fork has: a one-step
offset (`parallaxShift`, OpenMW's `eye.xy * (height * 0.04 - 0.02)`) on a primary or detailed hit with an
`_nh` map, in the inline-ray-query kernels. Nothing here was measured on the fork.

## Finding the visible texel

| method | reads per hit | precomputed | source |
|---|---|---|---|
| linear march, piecewise-linear refinement | 8 to 50, by `dot(N, V)` | nothing | Tatarchuk 2006 |
| linear march, binary refinement | linear count + 5 to 6 | nothing | Policarpo, Oliveira, Comba 2005 |
| relaxed cone stepping | 15 cone + 6 binary, fixed | a cone ratio per texel, one channel | Policarpo, Oliveira, GPU Gems 3 ch. 18 |
| maximum-mipmap (quadtree) traversal | near-logarithmic, bounded by a cap | a max-reduced mip chain | Tevs, Ihrke, Seidel 2008; Drobot, GPU Pro 2010 |

- Tatarchuk: a linear search alone stair-steps at 8-bit height. Reading the profile as a piecewise-linear curve and
  intersecting the last segment in float removes it. The count is a linear function of the view angle, [8, 50] in
  her examples, and an in-shader mip threshold (4 in her examples) fades to plain normal mapping, with a blend
  across the threshold.
- Relaxed cone stepping: the chapter states that depth scale and tiling change only the ray, so the cone map stays
  valid when they change. The precompute is O(n²) per map as published, and the map must not be mip-filtered:
  a level takes the minimum ratio of its texels, or the map is read nearest. Fixed iteration counts, so no
  divergence between lanes.
- Maximum mipmaps: the precompute is one max-reduction per level, which Drobot timed at 0.15 ms at 256² and
  2.09 ms at 2048² on a GeForce 8800. The descent stops at a level, which is a level of detail for free.

## What RTX Remix ships

The open-source runtime (`NVIDIAGameWorks/dxvk-remix`,
`src/dxvk/shaders/rtx/concept/surface_material/opaque_surface_material_interaction.slangh`, read 2026-11-08):

- Default `rtx.displacement.mode` is `QuadtreePOM`, over a height texture whose mips hold maxima. `RaymarchPOM`
  is the alternative: a linear march, its count `lerp(max, max / 4, |dir.z|)`, the last two samples interpolated.
  `maxIterations` defaults to 64.
- The descent stops at a minimum level computed from the hit's texture gradients, and starts halfway down the
  chain, which its comment says needs fewer iterations. At level 0 the ray meets a bilinear patch whose corners
  are the minimum of the four texels around them: watertight and inside every box above it, at the cost of
  single-texel spikes.
- Displacement goes both ways: `displaceIn` and `displaceOut`, with the neutral height `in / (in + out)` on the
  triangle. The ray starts where it crosses the top of the relief.
- A shadow ray, and a bounce, first march the height field from the displaced point along their direction, a
  linear march that takes away visibility in proportion to how far each sample is under the surface (soft, not
  binary). Its comment says the quadtree was slower and had worse artifacts there. Switches, with defaults:
  direct lighting on, indirect lighting on, NEE cache on, ReSTIR GI on, a displaced surface met by a bounce off,
  "expensive", and perfect reflections off.
- Terrain is baked into one material, height included (`rtx_terrain_baker`), so a hit marches once.
- `gbuffer.slang` names POM only to enable it. No offset of the stored position or motion for the displaced point
  was found there.

## Real displacement in a ray tracer

- Projective displacement mapping (2025): prisms over each triangle as AABBs in the top-level structure, a march in
  a custom intersection. 20 to 60 ms for a 2048² image on an RTX 4090, 40 to 60 % faster than RMIP on primary
  rays. 108 bytes per face beside the map.
- RMIP (Thonat et al. 2023) and Thonat et al. 2021: bounds over the displacement map, intersected on the fly.
  An order of magnitude over earlier displacement structures, by its authors.
- `VK_NV_displacement_micromap` is deprecated by `VK_NV_cluster_acceleration_structure`, "not a one-to-one
  replacement"; NVIDIA's toolkit says the extension "is no longer available" and points to RTX Mega Geometry.
  Both are NVIDIA's alone.

## Self-shadowing without a march

Horizon mapping (Max 1988; Sloan, Cohen 2000) stores the horizon's elevation in a few directions per texel and
answers a shadow ray in a read or two. Microsoft holds US 7,102,647 and US 7,170,527 on the interactive form.

## Motion and history

Unity's POM node outputs a pixel depth offset for effects that read depth. No source found states how an engine
moves a motion vector to the displaced point.

## Sources

- Tatarchuk, "Practical Parallax Occlusion Mapping with Approximate Soft Shadows", SIGGRAPH 2006 course:
  https://advances.realtimerendering.com/s2006/Chapter5-Parallax_Occlusion_Mapping_for_detailed_surface_rendering.pdf
- Policarpo, Oliveira, "Relaxed Cone Stepping for Relief Mapping", GPU Gems 3 ch. 18:
  https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-18-relaxed-cone-stepping-relief-mapping
- Drobot, "Quadtree Displacement Mapping with Height Blending":
  https://dcmpx.remotevs.com/org/drobot/www/PL/pub/M_Drobot_Programming_Quadtree%20Displacement%20Mapping.pdf
- Tevs, Ihrke, Seidel, "Maximum Mipmaps for Fast, Accurate, and Scalable Dynamic Height Field Rendering", I3D 2008:
  https://pure.mpg.de/pubman/item/item_1325622_5/component/file_3590464/i3d08.pdf
- RTX Remix runtime: https://github.com/NVIDIAGameWorks/dxvk-remix
- Projective Displacement Mapping for Ray Traced Editable Surfaces: https://arxiv.org/html/2502.02011v1
- RMIP: https://research.adobe.com/publication/rmip
- Tessellation-free displacement mapping for ray tracing:
  https://research.adobe.com/publication/tessellation-free-displacement-mapping-for-ray-tracing
- `VK_NV_displacement_micromap`: https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_displacement_micromap.html
- Displacement MicroMap Toolkit: https://github.com/NVIDIAGameWorks/Displacement-MicroMap-Toolkit
- Unity Parallax Occlusion Mapping node:
  https://docs.unity3d.com/Packages/com.unity.shadergraph@17.0/manual/Parallax-Occlusion-Mapping-Node.html
- Sloan, Cohen, "Interactive Horizon Mapping", EGSR 2000: https://unpaywall.org/10.1007%2F978-3-7091-6303-0_25
