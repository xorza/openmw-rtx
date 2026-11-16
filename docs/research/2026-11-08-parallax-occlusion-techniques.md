# Parallax occlusion mapping: what the field does — 2026-11-08

A survey of published methods and of one shipping path tracer, read against what the fork had then: a one-step offset
of OpenMW's (`eye.xy * (height * 0.04 - 0.02)`) on a hit with an `_nh` map. Nothing here was measured on the fork.

## Finding the visible texel

| method | reads a hit | precomputed | source |
|---|---|---|---|
| linear march, piecewise-linear refinement | 8 to 50, by `dot(N, V)` | nothing | Tatarchuk 2006 |
| linear march, binary refinement | the march + 5 to 6 | nothing | Policarpo, Oliveira, Comba 2005 |
| relaxed cone stepping | 15 + 6, fixed | a cone ratio a texel | Policarpo, Oliveira, GPU Gems 3 ch. 18 |
| maximum-mipmap (quadtree) traversal | near-logarithmic | a max-reduced mip chain | Tevs, Ihrke, Seidel 2008; Drobot 2010 |

- **Tatarchuk**: a linear search alone stair-steps at 8-bit height; reading the profile as a piecewise-linear curve and
  intersecting the last segment in float removes it, and a mip threshold fades to plain normal mapping.
- **Relaxed cone stepping** stays valid under depth scale and tiling, but its precompute is O(n²) a map and the map must
  not be mip-filtered.
- **Maximum mipmaps** cost one max-reduction a level, and the descent stopping at a level is a level of detail for free.

## What RTX Remix ships

The open-source runtime (`NVIDIAGameWorks/dxvk-remix`, read 2026-11-08): `QuadtreePOM` by default over a height
texture whose mips hold maxima, a linear `RaymarchPOM` as the alternative (at most 64 steps), the descent stopping at a
level from the hit's gradients. Displacement goes both ways about a neutral height. Shadow rays and bounces march the
height field softly from the displaced point; its comment says the quadtree was slower there. Terrain is baked into one
material so a hit marches once.

## Real displacement in a ray tracer

Projective displacement mapping (2025) marches prisms over each triangle in a custom intersection: 20 to 60 ms for a
2048² image on an RTX 4090. RMIP (Thonat et al. 2023) bounds the map on the fly. `VK_NV_displacement_micromap` is
deprecated by NVIDIA's cluster acceleration structures and is NVIDIA's alone. Too slow or too narrow for the target.

## Self-shadowing without a march

Horizon mapping (Max 1988; Sloan, Cohen 2000) stores the horizon's elevation in a few directions a texel; Microsoft
holds US 7,102,647 and US 7,170,527 on the interactive form.

## Sources

- Tatarchuk, "Practical Parallax Occlusion Mapping with Approximate Soft Shadows", SIGGRAPH 2006:
  https://advances.realtimerendering.com/s2006/Chapter5-Parallax_Occlusion_Mapping_for_detailed_surface_rendering.pdf
- Policarpo, Oliveira, "Relaxed Cone Stepping for Relief Mapping", GPU Gems 3 ch. 18:
  https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-18-relaxed-cone-stepping-relief-mapping
- Tevs, Ihrke, Seidel, "Maximum Mipmaps for Fast, Accurate, and Scalable Dynamic Height Field Rendering", I3D 2008:
  https://pure.mpg.de/pubman/item/item_1325622_5/component/file_3590464/i3d08.pdf
- RTX Remix runtime: https://github.com/NVIDIAGameWorks/dxvk-remix
- Projective Displacement Mapping for Ray Traced Editable Surfaces: https://arxiv.org/html/2502.02011v1
- RMIP: https://research.adobe.com/publication/rmip
