# The textures read as images and samplers apart

Measured 2026-11-09. The scene's textures were three arrays of combined image samplers: the textures
through the sampler of each slot's wrap, the companions through the same, and the textures again
through the footprint's anisotropic sampler. They are now two arrays of sampled images and eight
samplers, four by wrap and four along the footprint, paired at each read by the wrap the slot's texel
word carries (`TEXTURE_WRAP_SHIFT`).

## The hang

On a low-end AMD integrated GPU under AMD's Windows driver
(`2026-10-22-amd-integrated-footprint-read-hang.md`), with the specular maps read along the footprint
through the pair:

- **`RtxVisibilityTest.aGlossyArmReflectsThroughTheArmsOwnCone` passed**, and so did every other gloss
  and specular test, the texture array's tests, and `aTextureIsReadPastItsEdgeAsItsFileWrapsIt`.

## Pictures

`shot --views=all --upscale=off`, against the shaders before.

| shape | vanilla | PBR replacers |
|---|---|---|
| the pairs, the specular maps still read at the long axis's level | no trace moved | no trace moved |
| the pairs, the specular maps read along the footprint | no trace moved | 36 of 36 moved |

The PBR pictures that moved most, worst channel step and share of pixels moved:
`mournhold-arrival` 201 of 255 on 17.6%, `balmora-mages-guild` 196 on 14.8%,
`seyda-neen-boulder-close` 171 on 27.2%, `arkngthand` 153 on 1.2%, `probe-guild-planter` 145 on 10.2%.
What moved is what a specular map gives a surface seen at a slant: its lobe at the footprint's own
level and not the long axis's.

## Frame times

`bench --views=seyda-neen-ship,balmora-mages-guild`, release, medians in ms with the p99 after.

A high-end NVIDIA laptop GPU, three rounds each, before and after back to back, the mean of the rounds:

| content, place | before | after |
|---|---|---|
| PBR, ship | 6.805, p99 7.672 | 6.817, p99 7.812 |
| PBR, guild | 6.172, p99 6.846 | 6.225, p99 7.050 |
| vanilla, ship | 5.989, p99 6.870 | 5.979, p99 6.859 |
| vanilla, guild | 5.153, p99 5.902 | 5.145, p99 5.832 |

The PBR guild's 0.05 ms is the `trace` zone's 0.013 ms, `panes`' 0.008 ms and `landing`'s 0.005 ms.

The handheld's integrated GPU under RADV, vanilla, two rounds each, before, before, after, after:
ship 155.60, 155.57, 155.89, 155.91; guild 102.63, 102.61, 102.76, 102.77.

## Registers and shared memory

- **AMD's Windows driver**: the whole tuple's `media` 216 → 224 registers, four waves on an RDNA SIMD at
  either (1024 / 216 and 1024 / 224 both round down to four). No other kernel moved past its ceiling.
- **RADV**: `landing` with the dark alpha's read 112 → 128 registers and the fog's scatter 96 → 112,
  each under its ceiling; `water` and `panes` spill one or two scalar registers more.
- **NVIDIA**: the full tuple's `panes` without the dark alpha's read went from 168 registers and 5760
  bytes of shared memory to 128 registers, 6272 bytes and 160 bytes of local memory, which is what the
  whole tuple's `panes` holds.

## Descriptors

A change of anisotropy writes the eight samplers into each set, the four footprint samplers among
them, where it wrote three descriptors for every slot. The pool is two arrays of sampled images and eight samplers, where it was
three arrays of combined image samplers: under RADV a sampled image is 64 bytes and a combined one 96.
