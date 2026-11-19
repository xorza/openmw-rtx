# The handheld's three open issues, flown again — 2026-11-20

What a night's flights hold for the three open issues of `.notes/ISSUES.md`: the handheld's slow rooms after the land
legs, the structure block refused under the budget, and the PBR stand-in at Seyda Neen. Release builds,
`./omw release remote <handheld> tour --every=1`, back to back on one night: the PBR profile's first five legs and the
Fullrest Repack whole at master `7ccdebfbb4`, then the Repack whole at the baseline `ef34131d39` with the tour's frame
series applied, each leg's frames written (`--frame-times`). The records stand in `.tmp/handheld/`, ignored.

## The rooms after the land legs

| flight | rooms' legs' p99 median, ms | rooms' frame median | land's legs' p99 median | land's frame median |
|---|---|---|---|---|
| master | 28.6 | 16.5 | 82.7 | 27.3 |
| baseline | 22.8 | 16.3 | 85.2 | 27.5 |

68 of 99 room legs stand over 3 ms slower at master by their p99, none over 3 ms faster. The gap closes over the rooms:

| room legs | p99 median, master / baseline | game update's p99 median | main thread's cycles a frame |
|---|---|---|---|
| 56–69, after the land legs | 41.9 / 22.5 | 12.3 / 8.7 | +9% |
| 70–89 | 34.7 / 23.1 | 11.1 / 8.0 | — |
| 90–109 | 28.5 / 23.2 | 9.5 / 10.8 | +10% |
| 110–129 | 27.8 / 23.2 | 10.9 / 10.2 | — |
| 130–155 | 24.3 / 20.1 | 10.8 / 7.7 | +3% |

- **Each leg's slowest twentieth of frames**, the median over the room legs of master less the baseline: the frame
  +3.5 ms, the wait on the device −1.8, the game update +0.4, placing +0.1, handing over +0.1, the walk +0.15. The time
  is not in any one of the renderer's own columns on most legs.
- **On the worst legs placing and handing over are**: the Vivec Guild of Mages' and Nabith Waterway's slowest frames
  spend 7.5 ms placing and 7.5 handing over, against 0.3 at the baseline; Kogoruhn's Vault, Vassamsi Grotto, the Balmora
  Temple and the Winged Guar 2–2.4 each. Those legs' game update stands 0.8–4.1 ms over the baseline's too.
- **The main thread runs more cycles for 3% more instructions**: +9–10% over legs 56–109, and 7–10% more cache misses.
- **The device gives memory back across the rooms at master and never at the baseline**, its held blocks against the
  budget at a stop's end, MiB:

| stop | master | baseline |
|---|---|---|
| 1 | 2326 / 6144 | 2650 / 6144 |
| 30 | 5756 / 6015 | 4864 / 6144 |
| 55, the last land leg | 5500 / 5505 | 5056 / 6144 |
| 61 | 5392 / 5439 | 5082 / 6144 |
| 100 | 4630 / 4917 | 5146 / 6144 |
| 154 | 3990 / 4307 | 5146 / 6144 |

- **Master rests twice what the baseline rests**: 1421 MiB of textures at stop 61 against 739, and 2.5 times the
  resting vertices over the rooms.

## The structure block refused

- **A 64 MiB structure block found no device memory 251 times at master, from leg 25, and 10 times at the baseline,
  from leg 40.** At master a mesh was left out for it at legs 26, 34 and 42, the process holding 5223–5722 MiB of a
  5704–6144 MiB budget.
- **102 refusals at master were made beside resting content**: 3000–3950 resting textures of 1128–1612 MiB and
  10900–14500 resting meshes of 2256–2691 MiB, which nothing let go of to make the room.
- **Faults at master**: textures refused (leg 24), `water00.dds`' state refused (leg 25), meshes refused (leg 26, and
  two of `pc_help_character_01.nif` in `T_Test_PC`, leg 84), `quicksilver_bottle.dds`' state (leg 62), and the
  textures-readable check (leg 24). The baseline met the same `water00.dds`, `quicksilver_bottle.dds` and
  `T_Test_PC` refusals and a texture at leg 7.

## The PBR stand-in at Seyda Neen

- **It stands at master**: a 0.4 MiB texture with no name refused at leg 3 frame 645, the texture pool's 4288 MiB of
  blocks holding 4285.9 MiB allocated with its largest free range 0.1 MiB, while the heap held 5593 of 6144 MiB; the
  textures-readable check failed twice.
- **A second cause beside it**: `tx_b_n_argonian_f_n.dds` and `tx_b_n_argonian_m_n.dds`, BC5 normal maps, are drawn as
  stand-ins where a colour is read, at legs 3 and 4 (four times in the log).

## What these do not tell

- **Where master's main thread spends its extra cycles in the first rooms**: a CPU profile of legs 56–69 flown after
  the land legs, master against the baseline (`./omw profile`, with `--offcpu`), is what tells the game's update apart
  from the renderer's threads taking its cores and its cache.
- **The host's memory over the flights**: neither record holds the swap or the compaction stalls the earlier flights
  were read for (`2026-11-18-handheld-rooms-and-device-memory.md`), and the baseline's record holds no driver residency.
- **Which pass refuses before evicting**: the refusals name the resting content beside them, not whether eviction was
  asked first or how far behind it ran.
- **Why the BC5 maps are read as colour**: which model's material names them in a colour slot, or whether the
  replacer's naming is read as a base texture.
