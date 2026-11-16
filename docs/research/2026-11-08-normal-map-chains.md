# Normal map chains in the PBR replacers — 2026-11-08

The PBR profile (`openmw-pbr`: vanilla, Morrowind Visual Remastered, MVR PBR, Tamrielic PBR Armors), the extended PBR
profile (`openmw-pbr-all`: those, Tamrielic PBR Dwemer Ruins, Landscape Remastered 2.0) and the Fullrest Repack. DDS
levels decoded on the host.

## MVR PBR's normal maps are faulty below level 0

Black patches on the ground at a distance at cell (-1, -9), gone close up, are `tx_rm_grayrock_01_nh.dds` (MVR PBR):
its level 1 holds 23,171 texels of RGB (32, 36, 0), which decode to a horizontal normal and so to no light, under
level-0 texels whose alpha, the height, is 0. The coarser levels were filtered with the colour weighed by the alpha, as
a premultiplied image is: texconv filters through WIC with premultiplied alpha by default and zeroes the RGB where the
alpha is 0, and its `-sepalpha` is the switch for an alpha that is not coverage
([texconv](https://github.com/microsoft/DirectXTex/wiki/texconv)). A box chain of its level 0, alpha apart, draws the
ground with no patch.

Level 1 of each DXT5 `_nh` map against the box of its level 0, a texel more than 64 off on the mean with a blue under
128: 381 of MVR PBR's 1,188 are faulty (293 with 16 texels or more); Dwemer Ruins' and Landscape Remastered's 80 none.

## Its specular maps too, its colours rarely

The same test over every colour and data file that carries its chain, against the plain box and the box weighed by
alpha:

- **223 of MVR PBR's `_spec` maps are faulty**, in 217 of them the red that metal-roughness reads as metalness:
  `tx_a_dragonscale_spec` holds a red of 0 at level 0 and 253 at level 1, so a surface turns metal at a distance.
- **The colour chains are faulty in a few texels a file at most** (1,697 of 65,536 in the Fullrest Repack's worst), made
  by an ordinary filter whose sharpening and encoding error the threshold caught; vanilla's agree with their boxes.

So every normal and data map's chain is made again, and a colour file keeps its own.

## Normal maps in BC7

Every normal map made whole in BC7, its texels unit normals, `shot --views=all --upscale=off` against the build before
it (2026-11-09):

| BC7 search | the cold preparation | pictures' pixels moved by more than 8 of 255 |
|---|---|---|
| bc7enc's full search | about 270 s | median 0.5%, the close wall 10.9% |
| one mode a block: 5 with a height, 6 without | 64 s | median 0.32%, the close wall 4.58% |

One build shot twice moves 0.92% of the close wall. What moves is the relief: the march magnifies the heights' error,
and the normal view moves nothing visible. One mode a block made the heights closer to their source than the full
search (a mean error of 0.12–0.35 of 255 against 0.29–0.46) for normals a fifth of a degree further off.

## Loose chains in BC7, colours stored delit

- **Loose chains made whole in BC7**: 16 of 72 vanilla pictures moved past the denoiser's noise, the worst on animated
  fire and waterfall frames, which are small loose files; no picture moved more than 0.03% of its pixels by over 4.
- **Colours delit on the host and encoded again**: what moves is a byte along an edge, where the trace had divided the
  filtered read.

## What the preprocessor costs a start

`scene --views=seyda-neen-ship`, release, with keys made under a store that always misses: a key costs about 9.5 GB/s of
input, 3% of a shape's run and 2–14% of an alpha span's or a texel mean's. A walk that stops at the first texel that
answers costs less than its key (0.2 ms against 3.4 on the frame for a solid reach), since the key reads every byte.

## The device's derivations at arrival

`bench --suite=streaming` (`island-crossing`, twenty cell boundaries in ten seconds): the shading, spread, sprite bake
and ground derivations ran at a median of 0.02–0.10 ms on the frames that ran them and a worst of 1.1 ms. The worst
frames of a crossing are the host's, the game's update and the placement's uploads, 57 to 451 ms. So the derivations
stay on the device at arrival.
