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

## What the preprocessor costs a start

`scene --views=seyda-neen-ship`, release build, a high-end NVIDIA laptop GPU, two runs a profile, each figure the
range of the two. Milliseconds of each pass over the load, on the frame's thread and on the cell ring's reader, and with
keys made under a store that always misses (`ContentCache::sHolds` set true for the measurement alone):

| profile | pass | thread | asked | running | keying | keyed |
|---|---|---|---|---|---|---|
| `openmw-pbr` | shape | frame | 1394 | 86–89 | 2.1 | 19.3 MiB |
| | shape | ring | 4195 | 317–329 | 8.8–9.1 | 84.2 MiB |
| | alpha span | frame | 103 | 168–173 | 23.7–24.4 | 221 MiB |
| | alpha span | ring | 385 | 722–749 | 100–103 | 952 MiB |
| | texel mean | frame | 19 | 28–34 | 1.1 | 10.9 MiB |
| | solid reach | frame, ring | 38, 88 | 0.2, 2.7–2.9 | 3.4, 9.2–9.3 | 31, 88 MiB |
| Fullrest Repack | shape | frame | 1239 | 201–208 | 2.5 | 23.3 MiB |
| | shape | ring | 5327 | 1135–1163 | 14.8–15.1 | 144 MiB |
| | texel mean | frame | 19 | 305–311 | 7.0–7.1 | 67.8 MiB |
| | solid reach | frame, ring | 25, 101 | 0.2, 0.6–0.9 | 0.9, 5.2–5.4 | 8, 50 MiB |
| | solid fall | ring | 2 | 0.5 | 0.0 | 0.3 MiB |

- **A key costs about 9.5 GB/s of input** (952 MiB in 100 ms), 3% of a shape's run, 7–14% of an alpha span's and
  2–4% of a texel mean's. **A solid reach's key costs more than its run**, 3.4 ms against 0.2 on the frame: the walk
  stops at the first solid texel, and the key reads every byte.
- The passes come to 1.4 s on the PBR profile (0.29 on the frame) and 1.7 s on the Fullrest Repack (0.52 on the
  frame), over about 7.3 s from the session's start to the last log line before the world stood whole. The two
  threads run side by side.
- **On the frame's thread**, the PBR profile's alpha spans are 1.7 ms an ask, and the Fullrest Repack's texel means
  16 ms an ask.

## The shapes through the store

`scene --views=seyda-neen-ship` on `openmw-pbr`, release build, the same GPU, with the shape pass kept in a store of
its own, made empty for each cold run. **The machine was not quiet**: another build ran beside every run, at a load of
20 to 38, so a figure is good to its pair alone. A pair ran back to back at a load of 38.

| run | thread | asked | found | running | keying |
|---|---|---|---|---|---|
| cold | frame | 1394 | 108 | 240 ms | 5.0 ms |
| | ring | 4195 | 1255 | 907 ms | 22.4 ms |
| warm | frame | 1394 | 1256 | 182 ms | 5.0 ms |
| | ring | 4195 | 4195 | 0 | 20.9 ms |

- **The ring finds every shape warm**, and keys them in 21 ms where it ran them for 0.9 s cold. Cold, it shares the
  cores with the chain threads, which a cold store sets to work: 907–1090 ms against batch 0's 317–329.
- **The frame finds the small shapes and runs the 138 large ones again**, a quarter less: its asks reach the store's
  memory alone, which holds entries of 4 KiB or less. Its running moved from 85 to 240 ms cold and from 159 to 183
  warm between runs, with the load beside it.

## What the made chains hold

The same warm run, counted in the chain threads: the 1264 chains made come to 409 MiB of levels under the finest, and
the finest levels of those that keep their file's format to 1226 MiB, which the host held twice while each made image
copied its file's finest level. Cold, the chain threads ran for 335 s of processor time, about 42 s on their 8
threads.

## What the device's derivations cost at arrival

`bench --suite=streaming` (`island-crossing`, twenty cell boundaries in ten seconds, unsettled), release build, the
same GPU, after a throwaway leg, each derivation in its zone (`FrameZone::Shading`, `Spread`, `Bake`, `Ground`).
Milliseconds on the frames that ran it, of 597:

| profile | zone | frames | median | p99 | worst |
|---|---|---|---|---|---|
| `openmw-pbr` | shading | 77 | 0.062 | 0.338 | 0.338 |
| | spread | 125 | 0.103 | 0.818 | 0.826 |
| | bake | 7 | 0.049 | 0.056 | 0.056 |
| | ground | 344 | 0.101 | 0.343 | 0.353 |
| `openmw-pbr-all` | shading | 77 | 0.059 | 0.408 | 0.408 |
| | spread | 128 | 0.096 | 1.028 | 1.097 |
| | bake | 7 | 0.050 | 0.055 | 0.055 |
| | ground | 344 | 0.101 | 0.340 | 0.346 |
| Fullrest Repack | shading | 116 | 0.022 | 0.174 | 0.318 |
| | bake | 7 | 0.049 | 0.325 | 0.325 |
| | ground | 342 | 0.099 | 0.334 | 0.654 |

- **The frames around them**: a median of 5.1, 5.1 and 4.8 ms, a p99 of 20.4, 38.3 and 341 ms. The worst frames are the
  host's: the game's update (72 ms on `openmw-pbr`) and the placement's uploads (57 ms on `openmw-pbr-all`).
- **The Fullrest Repack's worst frames, 396 to 451 ms, are not in the renderer's zones**: the report gives each
  21.9 to 24.2 ms of the game's update and under 3 ms of anything else.

## The loading screen's preparation

`scene --views=seyda-neen-ship` on `openmw-pbr`, release build, the same GPU, a store of its own made empty for the
cold run, the preparation on 16 threads beside the kernels' compile. Whole runs, from the process's start to its end.

| run | models and layers read | preparation | whole run | peak resident |
|---|---|---|---|---|
| cold | 6755 of 6755 | 62 s | 72.5 s | 8.0 GB |
| warm | 0 of 6755 | 0.13 s | 9.6 s | 5.8 GB |

- **Cold, what it ran**: 22884 shapes asked (8.0 s), 4765 alpha spans (8.0 s, 9.9 GiB keyed), 56 texel means and
  6343 chains (940 s of processor time, 3.7 GiB keyed).
- **What play then found**: the ring's 4195 shapes and 385 alpha spans all, the chain threads' 1264 chains less 15,
  and the frame's large shapes as before, its asks reaching the store's memory alone.
- **Without the ground layers**, play made 68 chains and 53 alpha spans of the land's in the cold run. **Before the
  caches were emptied** during the preparation, a cold run peaked at 14.7 GB.

## The colour and data chains

Level 1 of every colour and data DDS file that carries its own chain, against two references made from its level 0:
the plain box, and the box with the colour weighed by the alpha. A texel counts where it is more than 64 from both on
the mean of its three channels, and only where the box's alpha and its own are 8 or more, since a BC1 hole decodes to
black. Normal maps are left out. A file counts with 16 or more such texels.

| install | data files with a chain | faulty | colour files with a chain | faulty (darker than both) |
|---|---|---|---|---|
| vanilla | 37 | 0 | 4504 | 1 (0) |
| `openmw-pbr-all` | 3613 | 343 | 4587 | 158 (62) |
| Fullrest Repack | 263 | 10 | 19 015 | 423 (216) |

- **The PBR install's faulty data maps are MVR PBR's `_spec` maps**, every one of the 223 traced to a loose file. In
  217 of them the channel that moves is red, which the metal-roughness layout reads as metalness: `tx_a_dragonscale_spec`
  holds a red of 0 at level 0 and 253 at level 1, so a surface turns metal at a distance. In
  `tx_a_glass_shield_spec`, 0 against 56.
- **The faulty colour chains are few texels a file.** The worst in the PBR install, `tx_w_dart_steel`, has 145 darker
  texels of 131 072; the worst in the Fullrest Repack, `tx_skyrim_basket_orc_02` and `pc_misc_ungorth_01`, have 1697 of
  65 536. Vanilla's chains agree with their boxes.

## Which filter the flagged colour chains match

Each flagged colour chain's level 1 against four references made from its level 0, by the mean distance over the
texels that show: the plain box, the box weighed by the alpha, the box in linear light (a 2.2 curve), and the
premultiplied box with no division back, which is the fault in MVR PBR's normal maps.

| install | flagged | plain | linear light | weighed by alpha | premultiplied |
|---|---|---|---|---|---|
| vanilla | 1 | 0 | 0 | 1 | 0 |
| `openmw-pbr-all` | 158 | 87 | 58 | 13 | 0 |
| Fullrest Repack | 423 | 241 | 136 | 43 | 3 |

The flagged chains were made by an ordinary filter, the threshold's texels being sharpening and the encoder's error.
Of the three that match the premultiplied box best, only the Fullrest Repack's `mfr/f/sm_leaves.dds` does so by a
margin (4.0 against 7.0); `pc_flora_milkthistle_atl` and `pc_red_servants01` are within 0.3 of the plain box.

## What a load costs the processor

`perf record -e task-clock -F 1999 --call-graph fp` over a whole `scene --views=seyda-neen-ship` run, release build,
the content cache holding nothing. Own time of a function, in milliseconds:

| function | `openmw-pbr` | Fullrest Repack | what it is |
|---|---|---|---|
| `RtxTool::digestScene`, and most of `MurmurHash3_x64_128` | 716, 876 | 918, 1123 | the harness's report of the scene, no game cost |
| `Rtx::alphaRange` | 552 | 0 | the relief's bounds, on both threads |
| `Rtx::PocketTree::firstFacing` | 130 | 449 | the shape fold |
| `Rtx::CreaseSplit::cutFan` | 92 | 260 | the normals' split |
| `rankMatrix` (`frame/bluenoise.cpp`) | 136 | 137 | the blue noise's rank matrices, made at every start |
| `Rtx::FogNoise::shared` | 53 | 58 | the fog's noise, made at every start |
| `Rtx::SceneExtractor::Traversal::enterWalked` | 56 | 165 | the walk |
| `Rtx::meanTexel` | 6 | 88 | the texel means |
| `dxtc_tool::isCompressedImageTranslucent` | 69 | 26 | OpenSceneGraph's own read of each DXT file, upstream's |
| `ESMTerrain::Storage::fillVertexBuffers` | 13 | 20 | the land's vertices, upstream's |
| `osgUtil::TangentSpaceGenerator` | under 1 | under 1 | the tangents the map visitor builds |

The cell reader spent 1.64 s on the PBR profile, most of it in the alpha walk and its block decode; on the Fullrest
Repack most of its time is the shape fold and the split. Upstream's own model and land reading is a small share of
either.
