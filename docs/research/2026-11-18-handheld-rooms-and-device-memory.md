# The handheld's slow rooms, and the device's memory — 2026-11-18

Why the handheld's Repack tour's rooms ran at a legs' p99 median of 51 ms on the branch against 21–22 ms at the
baseline (`2026-11-18-closing-tours.md`). Release builds, `./omw release remote <handheld> tour` of the Fullrest
Repack's profile, as the handheld stood the same afternoon. The handheld's content heap is its second, 6144 MiB of the
system's memory the driver maps for the device (GTT); "held" is what the process holds of it in blocks, "budget" what
the renderer holds content to there.

## Three whole tours and two of rooms alone

| flight | commit | rooms' legs' p99 median, ms | land's | device held, peak MiB | budget in the rooms, MiB | process peak MiB (swapped) |
|---|---|---|---|---|---|---|
| whole tour | `ef34131d39`, the baseline | 21.0 | 87.2 | 5081 | 6144 | 8570 (3423) |
| whole tour | `c8664cd001` | 51.0 | 79.8 | 5928 | 5346–5582 | 7259 (1607) |
| whole tour, `--memory-budget=4096` | `2c8d2ede85` | 27.9 | 78.1 | 3880 | 3368–3986 | 9199 (1701) |
| rooms alone | `a10f37724a` | 21.8 | — | 4328 | 5864–6144 | — |
| rooms alone, `--memory-budget=4096` | `a10f37724a` | 21.5 | — | 3688 | 4096 | — |

- **The rooms slow only after the land legs.** Flown alone they run at the baseline's pace; the land legs leave the
  device holding more than the rooms' budget.
- **The swap is not what slows them**: the baseline swapped twice what the branch did, and ran its rooms at 21 ms.
- **The baseline's budget is the driver's**, 6144 MiB throughout; the branch's is the lesser of the driver's and what
  the system leaves the process (`MemoryAllocator::systemBoundOf`), which falls as the host's side grows.

## What the device holds, and never gives back

The device's held memory, the blocks, rises over the land legs and never falls in any of the three whole tours (once,
by 2.5 KiB, on `c8664cd001`). Read at a stop's end:

| stop | baseline: held / budget | `c8664cd001`: held / budget | capped: held / budget |
|---|---|---|---|
| 1 | 2650 / 6144 | 2280 / 6144 | 2280 / 4096 |
| 30 | 4928 / 6144 | 5838 / 5964 | 3790 / 4096 |
| 55, the last land leg | 4992 / 6144 | 5838 / 5584 | 3798 / 3951 |
| 100 | 5081 / 6144 | 5928 / 5416 | 3880 / 3368 |
| 154 | 5081 / 6144 | 5928 / 5416 | 3880 / 3368 |

- **Over the budget**: the baseline at no stop; `c8664cd001` at 119 of 155, every one from stop 37 to the end; the
  capped flight at 91, from stop 61 on with gaps.
- **What the blocks hold free**, the rooms' median, MiB: 168 at the baseline, whose report reads one free figure for
  both; on the branch 229 in the textures' blocks and 711 in the structures', 1049 at stop 154, where the structures'
  blocks stand at 2184 MiB from stop 30 on.
- **What content holds**, the rooms' median, MiB: textures 1432 at the baseline, 2131 on the branch; structures 1856
  and 2184 in blocks.

## The Windows machine's memory past its heaps

At each of the Windows machine's Repack stops at `87b6f3a4d8` (`2026-11-18-closing-tours.md`), the private regions past
the process's heaps held what the device's two heaps held plus 147 to 211 MiB: its integrated GPU's memory is the
process's own. The machine has 27.8 GiB.

**What its meter called swapped is not paging alone.** Windows' figure is the private memory committed and not in the
working set, which counts the GPU's pages, what the system trimmed onto its standby list and what the page file holds
alike. The whole Repack flown at `2c8d2ede85` with the device held under 4 GiB:

| flight | device held, peak MiB | process peak MiB | of it not resident | heaps' regions at the end | legs' p99 median, all / land / rooms, ms |
|---|---|---|---|---|---|
| `87b6f3a4d8` | 8542 | 20048 | 7190 | 10.8 GiB | 38.9 / 101.0 / 34.3 |
| `2c8d2ede85`, under 4 GiB | 3876 | 15881 | 13251 | 11.3 GiB | 37.8 / 87.4 / 33.7 |

With less than half the device's memory the process held 13 GiB not resident against 7, and ran as fast: the frames do
not follow the figure.

## What the driver holds for the process (M1)

The whole Repack flown at `d5ea79cc75`, the kernel's DRM accounting read at each stop's end (`drm-total-` and
`drm-resident-` of every DRM client in the process's `fdinfo`). That commit summed each client once per open file, and
the handheld's Vulkan device is two files of one client, so its figures are halved here; the client's id now counts it
once. MiB:

| stop | held / budget | VRAM objects | of them resident in VRAM | GTT objects | resident in GTT |
|---|---|---|---|---|---|
| 1 | 2280 / 6144 | 2282 | 810 | 151 | 1623 |
| 25 | 5838 / 6144 | 5847 | 817 | 153 | 5183 |
| 55, the last land leg | 5838 / 5626 | 5847 | 817 | 155 | 5185 |
| 100 | 5928 / 5416 | 5937 | 817 | 159 | 5279 |
| 154 | 5928 / 5416 | 5937 | 817 | 159 | 5279 |

- **Every block the device holds is a VRAM object**, and all but the 817 MiB the carve-out takes stand in GTT, from
  the first stop on. Read at a stop's end the placement never moves: no stop shows the driver moving objects between
  the two.
- **The frames do not follow the budget.** The land stops over the budget ran at a p99 median of 76.3 ms (20 stops),
  those under it at 81.7 (35); the rooms, every one over it, at 61.9 — 51.0 on `c8664cd001`, so two whole flights of
  one build part by 10 ms.
- **The host was under pressure throughout**: the kernel stalled 974 allocations to compact memory, and the process
  held 1.9 GiB in swap at its peak, at the rooms' end.

## With the device giving blocks back

The whole Repack flown again with the unified split smoothed and the caches yielding to it, held counted by blocks,
and the emptiest structure and texture blocks emptied while the device is over its budget or a take was refused: on
the handheld at `4ca1857b03`, and on the Windows machine at `4ebe9c3153`, the same code but for the hang
`4ca1857b03` fixed, in moves its device never made.

| flight | legs' p99 median, all / land / rooms, ms | stops over the budget | device held, peak / at the end, MiB | moved, structures / textures, MiB |
|---|---|---|---|---|
| handheld, `d5ea79cc75` (M1) | 70.9 / 80.6 / 61.9 | 120 | 5928 / 5928 | — |
| handheld, `4ca1857b03` | 49.7 / 82.7 / 32.2 | 2 | 5838 / 4136 | 1171 / 2307 |
| Windows, `87b6f3a4d8` | 38.9 / 101.0 / 34.3 | 0 | 8542 / — | — |
| Windows, `4ebe9c3153` | 36.9 / 91.8 / 33.9 | 0 | 8542 / — | 0 / 0 |

- **The handheld's device follows its budget down**: over it at two stops, stops 68 and 69, by 12 MiB at most, and the
  held memory falls from 5838 MiB at the land legs' peak to 4136 at the end, under a budget that fell to 4511 as the
  host's side grew.
- **Its rooms run at 32.2 ms** against 51.0–61.9 for the whole flights before, and 21.0 at the baseline.
- **Refusals remain under the budget**: 3039 takes refused for room over the flight, nearly all of 0.4 MiB textures at
  the textures' ceiling, which keeps back what the uses before them owe their growth; one texture drew the stand-in for
  good and one 64 MiB structure block was refused, both at the land legs' peak, legs 24 and 25.
- **The Windows machine never held past its budget**, so it moved nothing, and ran as before.
