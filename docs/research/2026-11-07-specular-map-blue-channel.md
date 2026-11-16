# The blue channel of PBR specular maps — 2026-11-07

Every `_spec` map of MVR PBR (3,281) and Tamrielic PBR Armors Pt. 1 (115), at the top level of each file, the last data
folder winning as in the game.

## What the maps hold

- **B is near white with fine detail**: a median mean of 0.98, a median deviation of 0.05; 436 maps are flat white, none
  flat at another value, none black or inverted as a whole. R is nought on every MVR PBR map; only Tamrielic has metal.
- **Most of B is detail far smaller than a triangle of these meshes**: of its variance, a median 27% is under 1/128 of
  the texture's side and 55% between 1/128 and 1/16; a bake of the mesh's own shape would sit in the widest band.
- **MVR PBR's B has the shape of its height's cavities**: a median correlation of 0.75 with the cavity of the `_nh`
  height over 1,100 maps, 0.48 with the raw height.
- **The diffuse is already dark where B is dark**, by B's depth or more; the data cannot tell a dark paint from a
  painted shadow.
- **Tamrielic's low B reads as occlusion** in the four maps examined by eye: mail weave, gaps between plates, a quilt's
  folds, and one unused island.

## What other renderers do

- **glTF 2.0**: occlusion marks "areas that would receive less indirect lighting from ambient sources", and "Direct
  lighting is not affected"
  ([specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc)).
- **Filament**: the material's occlusion on indirect light, multi-bounce AO by Jimenez et al. 2016
  ([document](https://google.github.io/filament/Filament.md.html)).
- **Unreal's Lumen** takes the material's AO for self-occlusion; **Wareya's OpenMW-PBR** reads `ao = specTex.b` on the
  ambient terms; **Falcor and RTX Remix** have no occlusion slot; **Cycles** leaves a glTF occlusion map out.

## What it does to a picture

B read as an occlusion on indirect diffuse light alone, against the build that left it unread, 35 places on the PBR
profile: the mean difference under 1 of 255 at 34 places and 1.75 at `seyda-neen-fence`, whose mortar joints go darker;
noise and fireflies unchanged; vanilla's pictures the same to the hash; the frame within the spread.
