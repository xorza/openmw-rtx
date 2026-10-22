# The trace's spill ceilings on a Radeon 680M — 2026-10-22

Radeon 680M (RDNA 2, integrated), AMD proprietary driver 26.8.1, Windows 11. The full flavour,
`RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`, the full tuple (`sun moons sea maps`);
registers are `numUsedVgprs` of a 32-lane wave, scratch is `scratchMemUsageInBytes` a lane.

## What moved them

| Tree | `direct` registers | `panes` scratch |
|---|---|---|
| `fcb72e6a39` | 256 | 480 |
| without `65f2e7ddce` (full 32-lane subgroups) | 256 | 480 |
| without `65f2e7ddce` and `a82db11d77` | 244 | 208 |
| `fcb72e6a39`, the gather's `Gloss` and `RayAsk` again (`shading.glsl` before `a82db11d77`) | 239 | 496 |
| `fcb72e6a39`, `TEXTURE_SLOTS` 8192 | 256 | 192 |
| `fcb72e6a39`, `TEXTURE_SLOTS` 4096 | 256 | 192 |
| `fcb72e6a39`, a candidate's edges read whatever its cone | 256 | 480 |

- **`direct` rose with the gather that carries the lamp's draw and the glossy flag** in place of the
  ray and the `Gloss` record. It holds no scratch at 256, and 244 and 256 both leave four waves on a
  SIMD of 1024 registers a lane.
- **`panes` rose with the bindless array's growth from 4096 to 16384 slots.** At 16384 the third
  array, `texturesAlong`, starts 1.5 MiB into the set, and its descriptor loads add `0x180020` to a
  scalar register; a GFX10 scalar load's immediate offset reaches under 1 MiB, which 8192 slots keep
  it within. That is the likely cause and not a proven one.
- **`bounce` and `landing` held no scratch** on every tree above, under the 176 bytes their ceilings
  allowed, and `bounce` 128 registers.
