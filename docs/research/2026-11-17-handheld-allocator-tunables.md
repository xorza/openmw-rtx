# The handheld's tours with the swap counted, and glibc's allocator tunables — 2026-11-17

A handheld with a low-end AMD integrated GPU (RDNA 2) and 16 GB of unified memory, its video heap's budget 6144 MiB,
release build at `f45fb184d7`, one binary for every flight: the pool's hand-over (`f2251f7ac5`) and the memory left
counted without swap (`aaa81f2b63`). `tour --expected=<content>` at 800×600, the kernel's huge pages on every mapping
but where a flight turns them off (`--transparent-huge-pages=off`, `PR_SET_THP_DISABLE`). Tunables by
`GLIBC_TUNABLES`; no code changed between the flights. A flight that names tunables replaces the harness's own,
`glibc.malloc.hugetlb=1` (`Platform::restartOnHugePages`), so its heap is not advised onto huge pages: on the handheld,
whose kernel gives them to every mapping, that advice changes nothing; on the desk, whose kernel gives them only where
advised, the tuned flights' heap stood on small pages.

## The Fullrest Repack, 155 places, 100,919 frames

| flight | wall s | p99 ms | anonymous MiB at its most | of it swapped | the heap there: taken, free, arenas | faults |
|---|---:|---:|---:|---:|---|---:|
| default | 2,946 | 87.7 | 8,774 | 2,571 | 9,078, 1,075 free, 25 | 2 |
| `mmap_threshold=1048576` | 2,819 | 74.2 | 8,778 | 2,594 | 8,929, 1,074 free, 25 | 3 |
| the same and `arena_max=4` | 2,831 | 75.1 | 7,911 | 2,229 | 7,949, 1,041 free, 4 | 3 |
| huge pages off | 2,803 | 74.6 | 9,237 | 3,590 | 9,377, 1,075 free, 25 | 2 |

- The most anonymous memory stood at the closing leg but under four arenas, at leg 118, Uttumilk.
- The third fault of two flights is one mesh refused for room, a 64 MiB structure block; the default flight refused
  three such blocks with no fault for it. Every flight refused one texture that does not read and failed
  `textures-readable` 135 times, as the baseline's did.
- The reader held at most 209 MiB of room for 64 MiB of models in every flight, at leg 28.
- The geometry's host copy stood at 596–619 MiB at its most, at leg 48, as far as the device's 604–634 MiB.
- Content was refused room 61 times, the process 247–586 MiB under its budget each time (median 476): 38 textures of
  0.4 MiB, 12 of 1.4, 8 of 5.4 and 3 structure blocks of 64 MiB.

## The thresholds, at `64cad57e30`, before the swap was counted

The same tour a build before, its caches' budget still taking what the process swapped for memory left: every flight
swapped some 4–6 GiB, so its swap is no figure, and its heap's free memory is.

| flight | wall s | anonymous MiB at its most | of it swapped | the heap there: taken, free, arenas |
|---|---:|---:|---:|---|
| `mmap_threshold=262144` | 2,836 | 10,458 | 5,374 | 10,535, 830 free, 25 |
| `mmap_threshold=1048576` | 2,803 | 10,709 | 6,007 | 10,801, 683 free, 25 |
| `mmap_threshold=4194304` | 2,811 | 10,819 | 5,801 | 10,883, 774 free, 25 |
| `mmap_threshold=1048576`, `arena_max=4` | 2,827 | 9,348 | 3,906 | 9,312, 770 free, 4 |
| huge pages off, 149 places | 2,755 | 11,135 | 6,273 | 11,206, 799 free, 25 |

## The desk, the Repack's tour back to back, at `612a6e9f8f`

A high-end NVIDIA laptop GPU and 64 GB, 32 threads; nothing swapped. The first default flight followed a build and ran
cold (794 s, a p99 of 223 ms), and is left out.

| flight | wall s | p99 ms | worst ms | hitches | anonymous MiB at its most | the heap there: taken, free, arenas |
|---|---:|---:|---:|---:|---:|---|
| default | 391 | 18.1 | 391 | 9,294 | 10,637 | 10,723, 396 free, 32 |
| `mmap_threshold=1048576`, `arena_max=4` | 386 | 17.9 | 682 | 7,597 | 10,057 | 10,079, 97 free, 4 |
| the same again | 375 | 16.5 | 394 | 7,887 | 10,724 | 10,725, 97 free, 4 |

## The PBR stack, 93 places, 42,473 frames

| wall s | p99 ms | anonymous MiB at its most | of it swapped | the heap there: taken, free, arenas | faults |
|---:|---:|---:|---:|---|---:|
| 1,006 | 57.9 | 3,026 | 209 | 3,305, 674 free, 25 | 1 |

- 93 textures of 0.4 MiB were refused room, 417–699 MiB under the budget (median 540).
- The file's texels were asked 7,590 times for 13,787 MiB, beside 7,303 asks of the preparation: a file that stands as
  shipped is read again each time its slot arrives, since its levels are read only whole.
