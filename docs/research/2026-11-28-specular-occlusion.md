# A specular map's occlusion on the lobe — 2026-11-28

Prompted by replacers whose dark ground, bark, weathered doors, stone and cloth read as plastic under the trace: a
non-metal's 4% reflectance (`DIELECTRIC_F0`) reflecting the sky at grazing over a dark albedo.

## The content's albedo

Every terrain `_diffusespec` of MVR PBR (165), its RGB decoded from sRGB, a map's albedo the mean of its texels'
luminance:

- **The median albedo is 4.6%**, the tenth percentile 2.7% and the ninetieth 21%: charcoal and fresh asphalt reflect
  about 4%, and a dark soil's field, its own shadows in it, 8 to 13%.
- **The alpha's median is 0.88**, its tenth percentile 0.68: rough as a roughness, and as upstream OpenMW's "specular
  multiplier" (`files/settings-default.cfg`) a shine no dirt has, so these maps are authored for the roughness Wareya's
  shaders read.
- **SaintJ's Landscapes Remastered, its PBR version** (94 terrain `_diffusespec`): a median albedo of 4.2%, the tenth
  percentile 2.8%, its grass 2.8 to 4.6%; the alpha's median 0.82, mud 0.40 and a dirt road 0.92 to 1. Smoother where
  it is wet, so a roughness as well, and at 0.82 a non-metal's lobe reflects 2.2% looking straight down and 7.3% at a
  cosine of 0.1, over the grass's whole albedo.
- The object maps' blue had already shown the diffuse dark where the occlusion is (`2026-11-07-specular-map-blue-channel.md`).

At a cosine of 0.1 a rough non-metal's lobe reflects 5.4% of the light from every side at a roughness of one and
16.8% at a half, integrated over our lobe with its compensation; over an albedo of 3% that is more than the diffuse half
sends back.

## What the shaders the content was made for draw

Wareya's OpenMW-PBR (`shaders/lib/light/lighting_pbr.glsl`) reads the same 4% and no weight for it, but draws a
non-metal's lobe under the sun at half (`LIGHT_STRENGTH_SUN_SPECULAR`) and none under the ambient
(`PBR_SPECULAR_AMBIENT` off, the term gated by `metallicity > 0`): its non-metals reflect no sky, which a trace does.

## What renderers do with a material's occlusion

- **Filament** (`shaders/src/surface_light_indirect.fs`, `surface_ambient_occlusion.fs`): the indirect diffuse by
  Jimenez et al.'s multi-bounce, the indirect specular by Lagarde and de Rousiers' specular occlusion
  (`SpecularAO_Lagarde`, `Fr *= singleBounceAO(specularAO)`), and the sun by Chan's micro-shadowing
  (`computeMicroShadowing`, `surface_light_directional.fs`).
- **Unity's HDRP** (`CommonLighting.hlsl`, `MaterialEvaluation.hlsl`, `LitData.hlsl`, `TerrainLitData.hlsl`): the
  surface's specular occlusion from its own occlusion by the same fit, on terrain as well, multiplying every reflection
  it gathers (`lighting.indirect.specularReflected *= aoFactor.indirectSpecularOcclusion`), with Jimenez's multi-bounce
  at the specular colour (`GTAOMultiBounce(specularOcclusion, fresnel0)`); micro-shadowing an option.
- **glTF 2.0**: the occlusion is of "indirect lighting from ambient sources", and "Direct lighting is not affected".
- No renderer found scales a non-metal's F0 by its albedo, which cannot tell a painted shadow from dark paint.

## What it does to a picture

The lobe's indirect light under Lagarde's occlusion, against the build without it, every place on the PBR profile at
1920x1080 with the upscaler off (`shot --against`): 51 of 78 pictures moved, the 27 others within the denoiser's noise.
Each place's mean fell, by at most 0.103 of 255, but `sadrith-mora`'s, which rose by 0.007, and no more than 0.02% of
a picture's pixels went brighter by over one of 255. The most moved are `seyda-neen-wall-noon` and `seyda-neen-fence`,
6.4% and 5.6% of their pixels darker by more than one: the cleft between two boulders, and the joints between a
wall's stones, where the map's blue is dark. The worst pixel fell by 22 of 255, in `ahemmusa-yurt`.

Four kernels moved, the bounce's with the maps, and every kernel holds its ceiling on the desk's NVIDIA driver and
under RADV for the handheld's chip and the Windows machine's (`RtxVisibilityKernelsTest`, the drm-shim). On the
hardware, the handheld under its RADV and the Windows machine's AMD integrated GPU under AMD's driver
(`bench --frames=1 --pipeline-code`), the same four alone moved, by 47 to 71 instructions, at the 112 registers they
held and with no spill.
