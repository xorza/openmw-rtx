# The blue channel of PBR specular maps — 2026-11-07

Every `_spec` map of one PBR replacer profile, at the top level of each file: MVR PBR (Nexus 57592) and
Tamrielic PBR Armors Pt. 1 (Nexus 57486), over vanilla and MVR. The last data folder wins, as in the game. A
map over 1024 texels a side was box-filtered to 1024. B was read as stored. The diffuse was decoded from sRGB
to linear. No frame was rendered, so nothing here measures the effect on a picture.

## What the maps hold

| | all | MVR PBR | Tamrielic |
|---|---|---|---|
| maps | 3396 | 3281 | 115 |
| B flat white (deviation under 0.01, mean over 0.98) | 436 | 430 | 6 |
| B flat at another value | 0 | 0 | 0 |
| mean of B, median over maps | 0.980 | 0.981 | 0.922 |
| mean of B, tenth percentile over maps | 0.921 | 0.926 | 0.765 |
| first percentile of B, median over maps | 0.753 | 0.761 | 0.580 |
| deviation of B, median over maps | 0.051 | 0.050 | 0.087 |
| maps with a mean of B under 0.8 | 19 | 5 | 14 |
| mean of R, median over maps | | 0 | 0.794 |

- The format is BC1 for 3209 maps and BC3 for 187. B has five bits at each BC1 block end.
- 1742 maps have a deviation of B of 0.05 or more. 70 maps have a texel of B under 0.1.
- No map is black or inverted as a whole, which is the fault that Wareya's `PBR_IM_USING_BROKEN_AO_MAPS_PLS_TRASH_THEM_KTHX` handles.
- R is nought on every MVR PBR map. Only Tamrielic has metal.

## The scale of B

The share of the variance of B at each scale, over the 2960 maps whose B is not flat. The medians are not a
partition, so they do not sum to one.

| band, in fractions of the texture's side | median share | tenth percentile | ninetieth percentile |
|---|---|---|---|
| under 1/128 | 0.275 | 0.101 | 0.501 |
| 1/128 to 1/16 | 0.548 | 0.349 | 0.695 |
| over 1/16 | 0.145 | 0.047 | 0.336 |

Most of B is detail far smaller than a triangle of these meshes. A bake of the mesh's own shape would put its
variance in the widest band.

## B against the height

Over the 1100 MVR PBR maps with a non-flat B and an `_nh` height. The cavity is `min(h - blur(h, r), 0)`, with
the best of four radii from 1/128 to 1/16 of the side.

| | median r | tenth percentile | ninetieth percentile | r of 0.7 or more | r of 0.5 or more |
|---|---|---|---|---|---|
| B against the cavity | 0.754 | 0.590 | 0.878 | 758 | 1069 |

B against the raw height, over both packs, has a median r of 0.479. So much of MVR PBR's B has the shape of a
cavity map of its height. This does not prove how the author made it. Of the 436 flat maps, 65 have an `_nh`
and 409 have an `_n`.

## What the diffuse already holds

Pixels sorted by B over its own local mean (radius 1/32 of the side), against the albedo over its own local mean.

| B over local | MVR PBR pixels | B over local, mean | albedo over local, mean | Tamrielic pixels | B over local, mean | albedo over local, mean |
|---|---|---|---|---|---|---|
| under 0.7 | 6.7 M | 0.590 | 0.526 | 0.9 M | 0.460 | 0.611 |
| 0.7 to 0.85 | 17.4 M | 0.792 | 0.628 | 0.9 M | 0.787 | 0.758 |
| 0.85 to 0.97 | 59.7 M | 0.925 | 0.754 | 3.0 M | 0.928 | 0.907 |
| over 0.97 | 544.6 M | 1.019 | 0.988 | 19.1 M | 1.042 | 1.003 |

The diffuse is already dark where B is dark, by the depth of B or more. This data cannot tell a dark paint from
a painted shadow. The linear luminance of the diffuse has a median mean of 0.068 for MVR PBR (0.024 to 0.203
from the tenth to the ninetieth percentile), and 0.320 for Tamrielic.

## Tamrielic's B

In 16 of its 109 non-flat maps, B has an r of 0.5 or more against R. Eight maps with over 5% of non-metal pixels
have a mean B under 0.6 on those pixels. Four maps with a low B were examined by eye, with diffuse, R, G and B side by side:

- `tx_a_ironarmor_cuirass_01_spec`: the low B on non-metal pixels is an unused island, black in every channel.
- `tx_a_almindoril_ul_spec`: B is a weave of mail with a mean of 0.42.
- `tx_a_imperial_c_mail_spec`: B is white on the plates and black in the gaps between them, and the metal is on the
  plates. This is why B follows R.
- `tx_a_steel_pauldron_shadow_spec`: B is a quilt's folds, with a mean of 0.27.

All four read as occlusion. The README tells the player to set `SPECULAR_MAP_INTERPRETATION` to 2 in Rafael's
shader. That shader's source was not available, so what that value does to B is not known here.

## What other renderers do

- **glTF 2.0:** the occlusion texture marks "areas that would receive less indirect lighting from ambient sources",
  and "Direct lighting is not affected".
  [Specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc)
- **Filament:** a material's occlusion and the screen's are combined by `min`, and applied to indirect light. Medium
  scale goes in the map, and micro scale may stay in the base colour. Its desktop default is multi-bounce AO
  (Jimenez et al. 2016), and specular occlusion is Lagarde's (2014).
  [Document](https://google.github.io/filament/Filament.md.html),
  [`surface_ambient_occlusion.fs`](https://github.com/google/filament/blob/main/shaders/src/surface_ambient_occlusion.fs),
  [`ShaderGenerator.cpp`](https://github.com/google/filament/blob/main/libs/filamat/src/shaders/ShaderGenerator.cpp)
- **Unreal's Lumen:** GI takes the material's AO for "reliable self-occlusion on Skeletal Meshes".
  [Lumen](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine)
- **Blender's Cycles:** no node arrangement uses a glTF occlusion map as intended, so the map stays out of the
  render. [Blender manual](https://docs.blender.org/manual/en/2.80/addons/io_scene_gltf2.html)
- **Falcor and RTX Remix:** neither material has an occlusion slot. Remix takes a height for parallax occlusion
  mapping. [`BasicMaterial.h`](https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/Scene/Material/BasicMaterial.h),
  [`rtx_materials.h`](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_materials.h)
- **Wareya's OpenMW-PBR:** `ao = specTex.b`, on the ambient terms. On non-high-performance settings, a hack that its
  comment calls "physically wrong" also takes some of the diffuse colour off, direct light included.
  [`lighting_pbr.glsl`](https://github.com/wareya/OpenMW-PBR/blob/main/shaders/lib/light/lighting_pbr.glsl)
- **Unity's HDRP and Uncharted 4:** micro shadows estimate direct-light shadows from the AO and normal maps.
  [HDRP](https://docs.unity3d.com/Packages/hdrp/manual/Override-Micro-Shadows.html)

Not read at the source, because Nexus refuses the fetch: Rafael's shader. A Nexus summary lists "parallax AO" in its
Enhanced PBR Lighting, and another mod's summary names "Rafael's PBR neighbour-sample AO".
