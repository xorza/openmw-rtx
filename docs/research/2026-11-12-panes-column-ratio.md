# A layer's water column taken off the media stage's march

Measured 2026-11-12, on a low-end AMD integrated GPU (RDNA 2) under AMD's Windows driver 26.8.1, by
`RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`: registers are `numUsedVgprs` of a 32-lane wave,
scratch is `scratchMemUsageInBytes` a lane. Each `panes` module below was compiled with the build's own
commands and put in the test's shader folder alone, so every other kernel and the device stayed the
tree's.

## What moved the ceiling

`panes` held 80 bytes of scratch against a ceiling of 80 in both tuples the test reads, and then 272 and 256.
The driver was the same one, and glslc and spirv-opt from vcpkg (shaderc 2026.2, SPIRV-Tools 2026.3) and
from the Vulkan SDK (2026.4) emit the same instructions for it, with only the ids numbered apart.

| `panes` module | without the dark alpha's read | whole tuple |
|---|---|---|
| HEAD (`9083b628f3`) | 272 | 256 |
| HEAD, `VisibilityConstants` laid out as before `mPaneTracks` | 80 | 80 |
| HEAD, the sheet's span fetched in `sheetAt` again | 80 | 256 |
| HEAD, every shader change since `11086c7421` taken out | 80 | 256 |
| HEAD, the stage record read again after the peel | 256 | 240 |
| `11086c7421`'s source | 80 | 80 |

`mPaneTracks` and a pad, set in the middle of `VisibilityConstants`, moved every field after them by eight
bytes, and no line of `panes` that reads them changed. Every tuple of `panes` with the sea stood at 256
registers, the most a wave holds, and every tuple without it at 231 or under, none spilling:

| `panes` registers | tuples |
|---|---|
| 192 | 34 |
| 217–231 | 22 |
| 247–256, no scratch | 26 |
| 256, 64–272 bytes of scratch | 14 |

## Where it spilled

AMD's ISA of the whole tuple (`--pipeline-code`): 39 scratch stores, all but a few ahead of the first
traversal, and 97 loads, nearly all after the last. The spilled values are those made before the peel and
read after it: the packed stage record and the pixel's coordinates, and the two stacks, the nearest layer
and the end's motion, each written once a layer outside the traversal.

## What holds the sea's tuples at the edge

Every tuple of `panes` compiled with the column under the water taken out of the peel (the eye's medium
the air's alone) held no scratch, at 248 registers at most.

## A layer's column off the media stage's march

Each layer's column in its closed form, the beam scaled by the ratio `media` marched over the eye's whole
column (`STAGE_SHAFTS`), and `panes` tracing no ray for it:

| kernel, tuple | before | after |
|---|---|---|
| `panes`, without the dark alpha's read | 256 registers, 272 bytes | 248, none |
| `panes`, whole tuple | 256 registers, 256 bytes | 254, none |
| `panes`, every tuple | up to 272 bytes in 14 | none in any, 254 at most |
| `media`, whole tuple | 224 | 220 |
| `media`, without the dark alpha's read | 192 | 192 |

`panes` starts a ray query at two places where it started three. Of the 36 places the harness shoots,
none an eye under the water with a layer in view, every frame drew what it drew before, and every map tile
of the same size did but the storm's at night, by 2 of 255 on 0.12% of its pixels: a place one build draws
apart on 83% of its frame when it is shot alone and not after the others.
