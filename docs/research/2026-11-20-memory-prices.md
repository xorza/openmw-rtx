# What a device charges for what content stands in — 2026-11-20

What three drivers state for a texture, a structure and an allocation, asked with `openmw-rtxtool info --prices`
(`Rtx::priceMemory`, nothing made) at `f1c5c806fa`: the desk's NVIDIA card under NVIDIA's Linux driver 620.73, the
Windows machine's AMD integrated card under AMD's driver 26.8.1, and the handheld under RADV (Mesa 26.2.0-devel).
The figures the size classes, the slabs and a floor are to be cut by (`.notes/review-rtx-renderer.md` at
`a66d42e374`, "Before anything is built"). A texture is a square with its whole chain under it, as `Texture::describeFile` describes a
file's, which is also what a larger texture standing from that level asks.

## A texture

Bytes the device asks, and what of them is over the texels:

| square, chain | texels | desk | Windows | handheld |
|---|---|---|---|---|
| BC1 1 | 8 | 512 | 256 | 256 |
| BC1 8 | 56 | 2 048 | 1 024 | 4 096 |
| BC1 64 | 2 744 | 5 120 (+2 376) | 3 840 (+1 096) | 4 096 (+1 352) |
| BC3 64 | 5 488 | 7 680 (+2 192) | 6 400 (+912) | 8 192 (+2 704) |
| RGBA8 64 | 21 844 | 23 552 (+1 708) | 22 528 (+684) | 24 576 (+2 732) |
| BC1 128 | 10 936 | 13 312 | 12 032 | 12 288 |
| BC1 256 | 43 704 | 46 080 (+2 376) | 44 800 (+1 096) | 65 536 (+21 832) |
| BC3 256 | 87 408 | 89 600 (+2 192) | 88 320 (+912) | 131 072 (+43 664) |
| BC1 1024 | 699 064 | 701 440 | 700 160 | 720 896 (+21 832) |
| BC3 1024 | 1 398 128 | 1 400 320 | 1 399 040 | 1 441 792 (+43 664) |
| BC3 4096 | 22 369 648 | 22 371 840 | 22 370 560 | 22 413 312 (+43 664) |
| RGBA8 4096 | 89 478 484 | 89 480 192 | 89 479 168 | 89 522 176 (+43 692) |

BC2, BC5 and BC7 ask what BC3 asks and BC4 what BC1 asks, on all three; BGRA8 what RGBA8 asks.

- **What is over the texels is a constant a format, not a share**: 1.7–2.4 KiB on the desk, 0.7–1.1 KiB on Windows,
  from the smallest chain to the largest. A texture of 64 texels a side is 1.2–1.9 times its texels; one of 1024 is
  within 0.4%.
- **The handheld rounds to its alignment, and its alignment moves with the size**: 256 bytes below 8 texels a side,
  4 KiB from 8 to 128, and 64 KiB from 256 up. A BC1 square of 256 asks 65 536 bytes for 43 704 of texels, half as
  much again; from 1024 up the 64 KiB is under 3%.
- **A size is no multiple of its alignment on the desk**: 512 bytes for a texel, 1 536 for four, at an alignment of
  1 024. A slot's size is the size asked rounded up by the slot's own rule, not the driver's.
- **Alignments**: 1 024 on the desk at every size, 256 on Windows at every size, 256 to 65 536 on the handheld.

## A floor of 64 texels

What every texture standing at a 64-texel chain at least would hold, for 4 000 textures, two in three of them taken
as BC1 and one in three as BC3, a mix assumed and not counted:

| | BC1 64 | BC3 64 | 4 000 textures |
|---|---|---|---|
| desk | 5 120 | 7 680 | 22.8 MiB |
| Windows | 3 840 | 6 400 | 17.9 MiB |
| handheld | 4 096 | 8 192 | 20.8 MiB |

Under half a per cent of the handheld's 6 GiB heap: a floor is affordable on all three.

## A structure, as first built

For a mesh that does not deform, by its triangles (`BottomLevelStore::buildFlagsOf(Still)`), before it is made tight:

| triangles | desk | Windows | handheld | handheld's scratch |
|---|---|---|---|---|
| 1 | 2 816 | 720 | 384 | 65 780 |
| 16 | 3 840 | 3 148 | 2 752 | 67 360 |
| 256 | 17 664 | 42 124 | 40 832 | 93 280 |
| 4 096 | 235 648 | 666 764 | 650 112 | 566 368 |
| 65 536 | 3 680 000 | 10 661 004 | 10 398 592 | 8 194 144 |
| 262 144 | 14 697 472 | 42 642 572 | 41 593 728 | 32 638 048 |
| a triangle, large meshes | 56 | 163 | 159 | 125 |

- **A structure is three times the size on AMD's two drivers**: 159–163 bytes a triangle against 56. The handheld's
  structures weigh as its textures do, where the desk's are a third of that.
- **The handheld's build scratch has a floor of 64 KiB**, whatever the mesh; the desk's is 1 280 bytes and Windows'
  308. What a structure comes to once made tight is not stated before a build, and is not priced here.

## Kinds of memory, and limits

| | kinds a texture takes | kinds a structure's buffer takes | allocations | buffer to image |
|---|---|---|---|---|
| desk | 0, 1: the system's and the device's own | all five | 4 294 967 295 | 1 024 bytes |
| Windows | 0–7 of sixteen | the same eight | 4 294 967 295 | 1 byte |
| handheld | eight of eleven | the same eight | 4 294 967 295 | 1 byte |

- **A texture and a structure's buffer share a kind of the device's own on every machine**: one arena's kind holds
  both. On the desk a buffer and an image in one allocation keep 1 024 bytes apart, the textures' own alignment
  there.
- **No machine bounds the allocations**: the count is no constraint on the chunks' size.

## What it settles

- A floor of a 64-texel chain a texture: affordable, about 20 MiB for 4 000 textures.
- The ladder's steps: the handheld's 64 KiB rounding from 256 texels up is coarser than eight steps an octave below
  512 KiB, so classes finer than 64 KiB buy nothing there between 64 KiB and 512 KiB; below 64 KiB its granule is
  4 KiB.
- A slot's size is rounded by the arena, since no driver's size is its alignment's multiple on every machine.
- One arena kind for textures and structures: possible on all three.
