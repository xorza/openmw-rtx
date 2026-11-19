# The shadow rays' lift past a ledge — 2026-11-19

A high-end NVIDIA laptop GPU, release builds, vanilla, `shot --size=1280x720 --upscaler=off --filter=false
--jitter=false` at a view of the house beside Seyda Neen's ship, Clear: a ledge over the wall under the eave, the sun in
front of it and above. The rows below at 10:39, the rest at 10:29.

## What the wall showed

Luma means over the 300 pixels of each row from 700 to 1000, down from the ledge:

| rows | what | luma |
|---|---|---|
| 243–279 | the ledge's underside | 14–27 |
| 282–306 | the wall just under it | 56–71 |
| 309–327 | the wall under that | 39–57 |

A lit band between the ledge and its own shadow, whose edge ran wavy along the wall where the ledge is straight.

## Why

`Surface::mLift` (Hanika's offset, `smoothLift`) painted per pixel: over 15.5 units under the ledge, the probe's top,
along the facet's normal, on one wedge of the wall that is a single large triangle; about one unit a few dozen rows
down, at the next triangles. The wall's corners carry normals that lean a little off its plane, and over a triangle that
large the surface they describe stands that far off it. Every shadow ray left from there, past the ledge's lip. With no
lift, the ledge's shadow meets the ledge and its edge runs straight.

## The cutoff

Lifted only where the facet's cosine to the light is under a cutoff, fading to nought at it, as Cycles' `shadow_ray_offset`
weighs its offset (`shadow_terminator_geometry_offset`, a tenth by default):

| cutoff | the ledge | `seyda-neen-boulder-close` |
|---|---|---|
| none (every ray lifted) | lit band | its facets rounded over |
| 0.1 | shadow at the ledge | hard wedges where one facet shadows the next |
| 0.5 | shadow at the ledge | as with every ray lifted |

At 0.5 against every ray lifted, `--views=all --map`: 34 of 74 pictures moved, none with a new edge on inspection; the
largest shares at `seyda-neen-boulder-close` (98%), `seyda-neen-boulder` (74%), `seyda-neen-sail` (61%),
`dagoth-ur-caldera` (50%) and `seyda-neen-pond` (36%), by under a tenth of the range at most pixels.

## Cost

A second point held through a path end's traversals, the lamp's beside the sky's, took NVIDIA's `landing` from 384 to
1408 bytes of shared memory, past its ceiling (`RtxVisibilityKernelsTest`); one point for the two, the one lifted less,
stands under it.

## Sources

- [Hanika, "Hacking the Shadow Terminator", Ray Tracing Gems II, ch. 4](https://link.springer.com/chapter/10.1007/978-1-4842-7185-8_4)
- [Cycles `light/sample.h`](https://github.com/blender/cycles/blob/main/src/kernel/light/sample.h), `shadow_ray_offset`
- [Cycles `scene/object.cpp`](https://github.com/blender/blender/blob/main/intern/cycles/scene/object.cpp), the
  default of `shadow_terminator_geometry_offset`
