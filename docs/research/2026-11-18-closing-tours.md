# The tree-and-content-memory plan's closing tours — 2026-11-18

`./omw release tour` of vanilla, the Fullrest Repack and the PBR stack on the three machines of
`2026-11-17-baseline-tours.md`, held against that note's two flights of each: each leg's worst frame against the worse
of the two plus their spread, and the median over the legs of each leg's p99.

- **The desk**: in use through every flight, so its frame times are not read; its faults and memory are.
- **The handheld**: Steam and the desktop held 1.2–1.6 GB of its memory as a GPU's through every Repack flight of this
  round (`fdinfo`), which left its video heap a budget of about 4.8 GB, against the 6.1 GB the baseline was flown at.
  Its Repack figures are read with that.
- **The Windows machine**: built at each commit in its own clone.

## What the flights show

| machine | content | commit | faults | legs' p99 median, ms (baseline) | process peak MiB (swapped) |
|---|---|---|---|---|---|
| desk | vanilla | `9f41093017` | 0 | not read | 2156 (0) |
| desk | Repack | `9f41093017` | 0 | not read | 11572 (0) |
| desk | PBR | `9f41093017` | 0 | not read | 3313 (0) |
| handheld | vanilla | `9f41093017` | 0 | 18.4 (19.6, 18.8) | 2492 (4) |
| handheld | PBR | `9f41093017` | 1: the stand-in at Seyda Neen | 21.6 (22.2, 22.5) | 3317 (344) |
| handheld | Repack | `c8664cd001` | 2 refused, `textures-readable` at 1 stop | 57.4 (26.2, 26.0) | 7259 (1607) |
| Windows | vanilla | `87b6f3a4d8` | 0 | 27.1 (27.0, 26.0) | 4651 (2556) |
| Windows | Repack | `87b6f3a4d8` | 0 | 38.9 (37.3, 38.0) | 20048 (7190) |
| Windows | PBR | `87b6f3a4d8` | 3 outruns | 28.6 (29.0, 29.0) | 14397 (4769) |

The handheld PBR's baseline outran 9 and 12 cells; this flight outran none. The handheld Repack's baseline failed
`textures-readable` at 147 stops; this one at 1. Flown again at `8cdf5be569`, where a file the device refuses waits
for what stands to move down for it, the handheld's PBR drew the stand-in at Seyda Neen at the same frame.

## The handheld's Repack, flown five times

| commit | what it holds | legs' p99 median, ms | structure takes refused | video budget, median MiB | texels read from files |
|---|---|---|---|---|---|
| `9f41093017` | item 14.6 | 50.3 | 3 | 4840 | — |
| `16a5de2beb` | 14.6 reverted | 51.5 | 3 | 5059 | 1.5 GiB |
| `3bbb19ffad` | item 21's defragmentation | 113 of 155 stops flown | — | 2959, at least 680 | 38.8 GiB |
| `37e2d756d7` | 21, the device's reserve half the caches' | 73.2 | 133 | 4862 | 0.4 GiB |
| `c8664cd001` | 21 reverted, the reserve kept | 57.4 | 5 | 5416 | 0.4 GiB |

The baseline read 19.4 GiB of texels from files. At `3bbb19ffad` every block the defragmentation gave back went to the
caches, whose budget rose from 1.9 to 3.2 GiB, and the device's budget, what the system leaves less the same gigabyte,
fell after it.

**Its room legs**: with nothing arriving, 62–64 of 99 room stops held a few frames of 30–40 ms in the uploader's
placement, against none at the baseline. A CPU profile of 120 s of them puts that time in the host waiting for the
device (`FrameRing`'s wait for the oldest frame, `finishReads`), while no timed zone of the device passes 8 ms at its
worst. In those rooms the device held 5.8 GB against its budget of 4.8–5.4 GB; the baseline held 5.0 GB against 6.1.

**Its refusals**: three 64 MiB structure blocks a flight, each with 887 MiB free in the texture pool's blocks, the
largest range 26 MiB, and 2.5 GiB of resting meshes beside them.

## Windows

- **The heap walked once a second**: the caches' budget read the heap's free memory through `HeapSummary`, which walks
  the process heap under its lock. On the PBR stack at `942563a569` the game stalled 1.16 s about once a second with
  no zone of the frame holding the time, the legs' p99 median 1206 ms; the Repack at `9f41093017` reached stop 41 in
  four hours, frames of 14–33 s, against the whole tour's hour at the baseline. At `87b6f3a4d8`, which reads nothing of
  the Windows heap, the PBR stack's p99 median is 28.6 ms.
- **What holds the private memory** (`Platform::Process::privateMemoryHeld`), at the PBR stack's stop 90 at
  `942563a569`: 2.8 GiB in the heaps' regions, 11.1 GiB in 955 other regions, the largest 68.6 MiB.
- **The meter's own walk** held each stop's end some 47 s on the Repack's 7 GiB heap, querying the system once an
  entry; at `3e42700a85` it queries once a region. At `87b6f3a4d8` it still grows with the heap: 1 s at the Repack's
  first stop, 14 s at stop 41, 25 s at its last, past the monitor's hang watch from stop 100 on, which took 43 dumps of
  a game that had faulted nothing.
