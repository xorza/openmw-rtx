# Normal map chains in the PBR replacers

Measured 2026-11-08 on the PBR profile (`openmw-pbr`: vanilla, Morrowind Visual Remastered, MVR PBR, Tamrielic PBR
Armors), the extended PBR profile (`openmw-pbr-all`: the same, Tamrielic PBR Dwemer Ruins, Landscape Remastered 2.0) and
the Fullrest Repack. The DDS levels were decoded on the host with a BC3 decoder written for the purpose.

## The black patch at Seyda Neen

A view at cell (-1, -9), `--pos=-2591.5,-69007.0,1991.9 --look=-2205.8,-68172.2,1598.9`, hour 12.6, Clear, shows black
patches on the ground at a distance, which go when the camera comes near. They are the same with `--relief=false`. The
albedo view shows none; the normal view shows each patch as one flat normal.

- **The map**: `tx_rm_grayrock_01_nh.dds` (MVR PBR), 4096 square, DXT5, 13 levels, which no placement within 2500 units
  of the camera wears, so the ground's. Found by replacing the 381 faulty maps below with flat ones (the patch goes)
  and halving the set.
- **Its level 1** holds 23 171 texels of RGB (32, 36, 0) and alpha 0, where the box of the four level-0 texels above each
  is RGB (118, 131, 244). Every such texel lies under level-0 texels whose alpha (the height) is 0. The level-0 texels of
  alpha 0 hold ordinary normals: their mean RGB is (119, 129, 242).
- **Decoded**, (32, 36, 0) is (-0.75, -0.72, 0) with no blue, which `decodeNormal` reconstructs to a horizontal normal:
  the light at that texel goes to nought.
- **The cause**: the coarser levels were filtered with the colour weighted by the alpha, as a premultiplied image is.
  texconv filters through WIC with premultiplied alpha by default and zeroes the RGB where the alpha is 0, and its
  `-sepalpha` is the switch for an alpha that is not a coverage
  ([texconv](https://github.com/microsoft/DirectXTex/wiki/texconv)).
- **The proof**: the same file with a box chain of its level 0, alpha filtered on its own, loaded from a data folder
  after MVR PBR, draws the ground with no patch.

## How many maps

Level 1 of each DXT5 `_nh` map against the box of its level 0: a texel whose RGB is more than 64 off the box on the
mean of the three channels, with a blue under 128, is counted. BC3's own error against the box is 3.1 on the mean.

| maps | DXT5 `_nh` | with a faulty texel | with 16 or more |
|---|---|---|---|
| PBR profile (all in MVR PBR) | 1188 | 381 | 293 |
| Dwemer Ruins, Landscape Remastered and its PBR | 80 (117 more in other formats, not read) | 0 | 0 |

The worst are `tx_rm_grayrock_01_nh` (23 171 texels), `tx_rm_rock_02_nh` (23 057), `tx_daed_metal_03_nh` (22 304) and
`tx_telv_podwalls_nh` (20 993). `_n` maps were not read: a DXT1 map has no alpha to weigh by.

## Formats

Normal maps (`_n`, `_nh`) and files with one level, across every data folder of each install:

| install | normal maps | files of one level |
|---|---|---|
| PBR (`Morrowind-PBR`) | DXT1 2762, DXT5 1273, loose 123, ATI2 3 | 2, both 2048 square |
| Fullrest Repack | DXT1 431, DXT5 26, DXT3 5, loose 1 | DXT5 3891, DXT3 912, DXT1 336, loose 259 |

No normal map is BC7. With each profile's later folders winning, the Fullrest Repack loads 5287 files of one level at
512 or less on a side and 111 past it.

## What the coarser levels weigh

The coarser levels of a chain are a third of its finest level. Over the files each profile loads, with later folders
winning:

| profile | normal maps | their finest levels | their coarser levels | files of one level, finest levels |
|---|---|---|---|---|
| `openmw-pbr` | 3839 | 3.31 GiB | 1.10 GiB | 0.00 GiB |
| `openmw-pbr-all` | 3858 | 5.12 GiB | 1.71 GiB | 0.00 GiB |
| Fullrest Repack | 463 | 0.22 GiB | 0.07 GiB | 0.19 GiB |
