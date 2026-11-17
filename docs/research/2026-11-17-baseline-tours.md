# The baseline's tours, before the content memory items — 2026-11-17

The tours every later item of the tree and content memory plan is held against: release builds at `ef34131d39`
(the meters of item 8, its harness running as the game: the timer caches expiring, the doors' reach the shipped one,
no huge pages asked), `tour --expected=<content>` at the tour's own 800×600, three contents a machine, each flown
twice so a leg's worst frame has its own spread beside it. The profiles name their own content; the desk's vanilla
profile names Morrowind alone, so its tour adds both expansions.

- **The desk**: a high-end laptop CPU beside a high-end NVIDIA laptop GPU.
- **The handheld**: a low-end AMD integrated GPU (RDNA 2), 16 GB of unified memory, its video heap's budget 6144 MiB,
  its kernel giving every mapping huge pages.
- **The Windows machine**: a desktop with a mid-range AMD discrete card of 12 GiB, MSVC's build of the same commit
  (`db026575b2`, whose change past `ef34131d39` is the driver's alone).

Every flight landed. Each tour writes its record (`tour.json`), its findings, and its log. A tour exits non-zero
where it found a fault: every repack tour off the desk, and the handheld's PBR tours.

## What every tour closed with

A leg's worst frame, its p99, and the median of the legs' p99 come from the record. The process, device and
texel figures are the peaks the log closes with: anonymous memory with what of it was swapped out, the device's
memory by use, and the texels read back from a file over the whole run (`TexelRead::File`).

| machine | content | flight | wall s | legs over 50 ms | over 100 ms | median leg p99 ms | worst ms, leg | process MiB (swapped) | device MiB | texels read from files MiB | faults |
|---|---|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|
| desk | vanilla | 1 | 77 | 3 | 0 | 1.7 | 86, leg-12-land-15,18 | 2849 (0) | 1687 | 82 | 0 |
| desk | vanilla | 2 | 78 | 3 | 2 | 1.6 | 109, leg-12-land-15,18 | 2858 (0) | 1689 | 82 | 0 |
| desk | mfr | 1 | 418 | 22 | 10 | 4.4 | 849, leg-39-land--16,8 | 10409 (0) | 7475 | 3886 | 0 |
| desk | mfr | 2 | 388 | 22 | 7 | 4.2 | 251, leg-41-land--98,11 | 10433 (0) | 7471 | 3867 | 0 |
| desk | pbr | 1 | 85 | 7 | 4 | 1.9 | 142, leg-2-land-22,-12 | 4319 (0) | 10656 | 7018 | 0 |
| desk | pbr | 2 | 83 | 2 | 0 | 1.9 | 86, leg-12-land-15,18 | 4836 (0) | 10652 | 7017 | 0 |
| handheld | vanilla | 1 | 830 | 17 | 2 | 19.6 | 135, leg-2-land-22,-12 | 2419 (0) | 2846 | 82 | 0 |
| handheld | vanilla | 2 | 834 | 17 | 1 | 18.8 | 136, leg-2-land-22,-12 | 2408 (0) | 2846 | 82 | 0 |
| handheld | mfr | 1 | 2816 | 58 | 47 | 26.2 | 897, leg-50-land--16,23 | 8443 (2111) | 4840 | 19913 | 2 |
| handheld | mfr | 2 | 2839 | 58 | 51 | 26.0 | 1753, leg-36-land-40,5 | 8655 (2506) | 4900 | 18883 | 2 |
| handheld | pbr | 1 | 1003 | 20 | 18 | 22.2 | 993, leg-6-land-22,0 | 3931 (555) | 5070 | 63638 | 10 |
| handheld | pbr | 2 | 999 | 20 | 16 | 22.5 | 254, leg-8-land-23,6 | 3958 (2) | 5068 | 63502 | 13 |
| Windows | vanilla | 1 | 980 | 19 | 13 | 27.0 | 999, leg-17-land--27,27 | 5404 (3361) | 2130 | 83 | 0 |
| Windows | vanilla | 2 | 975 | 19 | 12 | 26.0 | 952, leg-17-land--27,27 | 5429 (3368) | 2130 | 82 | 0 |
| Windows | mfr | 1 | 3616 | 68 | 51 | 37.3 | 11314, leg-49-land-49,23 | 19051 (7990) | 7686 | 8649 | 2 |
| Windows | mfr | 2 | 3683 | 64 | 50 | 38.0 | 12490, leg-49-land-49,23 | 19035 (7958) | 7677 | 8741 | 2 |
| Windows | pbr | 1 | 1167 | 18 | 18 | 29.0 | 2269, leg-2-land-22,-12 | 13607 (5175) | 9498 | 10573 | 0 |
| Windows | pbr | 2 | 1170 | 19 | 18 | 29.0 | 2241, leg-2-land-22,-12 | 13637 (5254) | 9497 | 10778 | 0 |

## The desk's slow legs

Below are the legs whose worst frame passed 100 ms on either flight, with what led its worst hitch (the findings'
`led by`). The other legs stayed under it on both flights.

| content | leg | worst 1 | worst 2 | p99 1 | p99 2 | median 1 | median 2 | led by |
|---|---|---:|---:|---:|---:|---:|---:|---|
| vanilla | leg-11-land--26,15 | 78.6 | 101.8 | 11.2 | 11.9 | 2.0 | 2.0 | update |
| vanilla | leg-12-land-15,18 | 86.2 | 108.8 | 12.0 | 12.2 | 1.9 | 2.0 | update |
| repack | leg-1-land--139,-58 | 239.9 | 219.8 | 23.8 | 17.1 | 2.5 | 2.4 | interface |
| repack | leg-15-land-13,-37 | 142.9 | 89.1 | 25.3 | 22.9 | 3.3 | 3.2 | walk |
| repack | leg-31-land--10,-4 | 196.6 | 207.2 | 20.3 | 19.5 | 3.8 | 3.6 | update |
| repack | leg-36-land-40,5 | 196.2 | 209.7 | 18.9 | 20.1 | 3.7 | 3.5 | update |
| repack | leg-37-land--101,5 | 690.0 | 69.3 | 87.0 | 37.6 | 15.0 | 5.7 | interface, update |
| repack | leg-39-land--16,8 | 848.8 | 37.9 | 55.8 | 16.7 | 5.2 | 3.5 | update |
| repack | leg-40-land-46,11 | 107.4 | 101.3 | 18.9 | 18.8 | 3.6 | 3.7 | update |
| repack | leg-41-land--98,11 | 250.9 | 251.0 | 32.5 | 41.3 | 4.8 | 5.0 | update |
| repack | leg-42-land--120,14 | 106.6 | 150.8 | 33.2 | 35.1 | 5.0 | 5.2 | update |
| repack | leg-155-closing--139,-58 | 181.1 | 182.0 | 18.4 | 19.2 | 3.6 | 3.8 | update |
| PBR | leg-1-land--1,-15 | 137.2 | 38.9 | 23.2 | 9.4 | 1.7 | 1.7 | update, place |
| PBR | leg-2-land-22,-12 | 142.3 | 16.4 | 15.0 | 12.3 | 1.9 | 1.9 | update |
| PBR | leg-11-land--26,15 | 100.1 | 77.3 | 12.8 | 12.6 | 2.1 | 2.1 | update |
| PBR | leg-12-land-15,18 | 108.2 | 86.0 | 11.9 | 12.0 | 2.0 | 2.0 | update |

## Content woken from rest

The first flight of each. Each tour's `rest reuse` line gives the textures' and meshes' bytes that woke from rest. Beside it is the share of
those bytes a least-recently-rested cache of each size would have kept (`RtxTool::RestReuse`).

| machine | content | woke MiB | 256 MiB | 512 MiB | 1 GiB | 2 GiB | 4 GiB |
|---|---|---:|---:|---:|---:|---:|---:|
| desk | vanilla | 3600 | 92.9% | 95.3% | 100% | 100% | 100% |
| desk | repack | 49024 | 34.6% | 53.0% | 80.0% | 95.9% | 98.4% |
| desk | PBR | 47101 | 25.5% | 43.3% | 64.9% | 87.7% | 94.9% |
| handheld | vanilla | 9093 | 61.9% | 86.9% | 95.0% | 100% | 100% |
| handheld | repack | 63665 | 27.1% | 41.2% | 65.5% | 97.7% | 100% |
| handheld | PBR | 20691 | 51.0% | 72.1% | 89.6% | 98.8% | 100% |
| Windows | repack | 72996 | 23.8% | 36.3% | 57.2% | 89.2% | 98.8% |
| Windows | PBR | 51126 | 24.6% | 41.3% | 63.6% | 87.7% | 97.0% |

## What this says before any item

- **The desk's frame is the game's, not the renderer's.** Every slow leg but one is led by the game's update or
  its interface, loading cells. Its two flights agree to within 0.2 ms on the median leg's p99. The single frames
  vary more: the repack's legs 37 and 39 took 690 and 849 ms on the first flight and 69 and 38 ms on the second.
  A comparison against this baseline reads the p99 and the count of slow legs before a leg's worst frame.
- **The handheld reads the same texels again and again.** It reads 19.9 GiB back from files under the repack
  against the desk's 3.9 GiB (5×), and 63.6 GiB under the PBR stack against 7.0 GiB (9×). Its device is full:
  the textures had 62 MiB of room at the repack's peak and 15 MiB at the PBR stack's. The meter does not say which
  reads are textures coming back after leaving for room.
- **On the handheld, room runs out before content does.** Both PBR tours refused a texture and outran their world,
  nine cells and twelve (`outrun`). Both repack tours refused a texture and failed `textures-readable` 147 times; the
  first's 132 looks for room in the caches found every entry in use. The repack swapped 2.1 and 2.5 GiB of some
  8.5 GiB both times; the PBR stack 555 MiB once and 2 MiB the second time. Vanilla faulted nowhere and swapped
  nothing.
- **The Windows machine counts as swapped what the desk does not.** Even vanilla held 5.4 GiB of anonymous memory
  there, 3.4 GiB of it counted out, against the desk's 2.8 GiB and nothing; the repack 19.0 GiB, 8.0 out, against
  10.4; the PBR stack 13.6 GiB with the heap taking 3.6 of it, against 4.3 in all. Its worst frames, 0.95–1.0 s on
  vanilla's leg 17, 11.3 and 12.5 s on the repack's leg 49, 2.2–2.3 s on the PBR stack's leg 2, sit on those tours.
  No meter yet says what holds the memory the heap does not, nor whether what Windows counts out was paged.
- **Content wakes from rest by the tens of gigabytes.** That is 47–73 GiB of textures and meshes over a repack or
  PBR tour on the desk and the Windows machine. A cache of 2 GiB of what rests would have kept 88–96% of it, and
  one of 512 MiB 36–53%.
