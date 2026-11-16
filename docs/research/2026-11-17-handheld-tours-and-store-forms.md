# The handheld's tours at `8c032f6e00`, and what forms the store holds — 2026-11-17

A handheld with a low-end AMD integrated GPU (RDNA 2) and 16 GB of unified memory, its video heap's budget 6144 MiB,
release build at `8c032f6e00`, `tour --expected=<content>` at 800×600. The build has the eviction for what the device
waits on and the structure storages' own blocks (`84a60139e8`), and logs the heap an arena apart (`e7368f11d5`). The
harness ran as it does: on huge pages, its preload reach scaled to the flight's speed.

## The Fullrest Repack: flown, 155 places in 62 minutes

| | |
|---|---|
| content takes refused | 331: 325 textures (236 of 0.4 MiB, 80 of 5.4 MiB, 9 of 1.4 MiB) and 6 structure blocks of 64 MiB |
| the process's video memory at a refusal | 4,039–5,086 MiB of the 6,144 MiB budget: 1.1–2.1 GiB under it |
| the texture pool at a refusal | 1,664 MiB in blocks, 1,327–1,649 MiB allocated, the largest range free 0.9–4.8 MiB |
| what rested beside a refusal | 2,269 textures on 930 MiB and 9,235 meshes on 2,121 MiB |
| faults | a texture refused, a mesh refused, `textures-readable` 147 times |
| image loads | 29,869, 18,493 of them again |
| NIFs parsed, templates made, collision shapes made | 125,678 (102,080 again), 104,753 (81,164 again), 82,823 (59,718 again) |
| anonymous memory, most resident | 6,424 MiB at stop 30, with 1,569 MiB swapped out |
| at the end | 6,085 MiB resident and 3,202 MiB swapped out; the heap 4,642 MiB free of 9,364 MiB taken |
| the heap's arenas at the end | 25; the most free 603 of 3,540 MiB, 519 of 546, 479 of 528, 452 of 480 |
| hitches | 307 findings, the worst frame of each a median 78.5 ms; the worst 3,939.5 ms, led by the update, in leg 1 |
| frames over 16.7 ms | most frames of every leg: 1,228 of leg 1's 1,376 |
| the first place flown again at the end | 4,683,631 vertices in use against 3,060,436, 719.6 MiB of live structures against 499.9, for the same 8,990 instances and 641 textures; the median frame 28.08 ms against 26.69 |
| woken from rest | 64.6 GiB; a rest of 1 GiB would have kept 64.6% of it, of 2 GiB 96.9% |

## The PBR replacers: aborted in leg 3

The process aborted 107 s and 3,590 frames in, in leg 3, on a contract of the backend's: "a sprite light bake names a
source that does not stand" (`TextureArray::make`), while a hand-over extended the scene by 28 textures. The catcher's
report holds the line; the run's own output did not. The tour of the same content a build earlier (`ee7852dc50`) flew
all 93 places.

| | |
|---|---|
| content takes refused before the abort | 42 lines, each a texture of 0.4 MiB |
| the process's video memory at a refusal | 4,988–5,034 MiB of the 6,144 MiB budget: 1.1 GiB under it |
| the texture pool at a refusal | 3,456 MiB in blocks, 3,445–3,455 MiB allocated, the largest range free 0.1–0.3 MiB |
| the heap after stop 1 and stop 2 | 957 MiB free of 3,075 MiB taken; 930 MiB free of 4,487 MiB, 254 MiB swapped out |
| image loads after stop 2 | 7,657, 1,382 of them again |
| outrun | six cells of leg 2 (6..7, -13..-11) never stood whole while the player was in their column |

## What it says

- **Neither tour came near its budget, and both were refused.** The process stood 1.1–2.1 GiB under the budget at every
  refusal and no new block was made: the room is withheld by content's ceiling (`MemoryAllocator::ceilingOf`), not
  taken.
- **The eviction for what waits did not serve.** Content was refused beside 3 GiB of rest.
- **Under the replacers the texture pool was full, not cut**: 99.7–100% of its blocks allocated. Under the Repack it was
  cut as well: up to 337 MiB free in the blocks with no range past 4.8 MiB, and textures of 5.4 MiB refused.
- **The resource caches held nothing of what they read**: a model's file was parsed five times over, the caches'
  budget standing under what was held while the heap kept gigabytes free.
- **Half the heap is free memory the allocator keeps.** At the Repack's end 4,642 MiB of 9,364 MiB, in arenas that are
  nine tenths free, beside 3,202 MiB swapped out. The same figures on small pages are not measured.

## The chains the desk's store holds

The store three profiles left on the desk, read by each entry's trailer and its chain's form byte: 126,695 entries,
13.67 GiB.

| chain form | entries | bytes |
|---|---|---|
| the file's own format, every level made (`Own`) | 18,472 | 7,384 MiB |
| whole in BC7 | 4,121 | 6,063 MiB |
| the levels under the file's finest (`Coarser`) | 3,154 | 233 MiB |

A chain's finest level is three times the levels under it, so the coarser chains kept whole would add about 700 MiB,
a twentieth of the store.
