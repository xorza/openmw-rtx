# What leads the tours' longest frames — 2026-11-17

Release build at `5db3a1fbf7`, the head of item 8 of the tree-and-content-memory plan. Two machines:

- **The desk**: a high-end laptop CPU beside a high-end NVIDIA laptop GPU.
- **The handheld**: a low-end AMD integrated GPU, with 16 GB of memory shared by the CPU and the GPU.

Each tour's ten longest frames are named in its findings with the monotonic second each ended on (`LongestFrames`).
The profiles were cut to those windows:

- `perf record -k CLOCK_MONOTONIC -e task-clock --call-graph fp`, at 499 Hz on the handheld and 999 Hz on the desk.
- On the desk, a second run sampled every context switch as well (`-e context-switches/period=1/`), so a window says
  where the frame thread waited as well as where it worked.

The desk was in use during its runs, so its frame lengths are read as shapes, not figures.

## The handheld

| content | longest | led by |
|---|---|---|
| PBR | 288 ms | a script's `GetDynamic` on an explicit reference: `World::searchPtr` → `CellStore::search` over every loaded cell store, 3,065 by then, with their pages read back from swap |
| PBR | 160–214 ms, four frames | `vkAllocateMemory` on the frame thread, the kernel clearing and remapping each new allocation's pages: 1.9 s of the tour in all, at most 38 ms of a 100 ms window. 451 samples for the normal spread's means, made to each batch's size exactly and let go past their room; 273 for the content pools' new blocks; 123 for staging blocks past the kept 64 MiB |
| PBR | 182–213 ms, walk-led | the frame thread off the CPU for most of the frame (below: a settled tour's walk) |
| Repack | 1,874 ms | `WorldModel::getAll` over the cell stores, half of it reading their pages back from compressed swap |
| Repack | 926, 445 ms | a content pool's new block, whose allocation made the kernel push other memory to swap first |
| Repack | 598, 395 ms | `CellStore::search` and `getAll` again |
| vanilla | 136 ms | the device (`finish`), not profiled further |

The handheld's Repack tour held 6.9 GB of heap, 1 GB of it free in four arenas, and swapped 0.9 GB before a 755 MB
file of this analysis, left in the handheld's tmpfs, pushed it further; the stops after it are not read. Its worst frames are memory pressure first.

## The desk

| longest | led by |
|---|---|
| 270, 200, 96, 89, 87 ms | `OpAddItem` → `World::getAll`, and `OpOnDeath` → `World::getPtr` → `CellStore::search`: 35–41% of each frame, over every loaded cell store |
| 115 ms | Lua's `objectAddedToScene` → `WorldModel::registerPtr`, 45% of the frame in its hash map as a cell loads |
| 76 ms | `LocalMapBase::updateDoorMarkers`, MyGUI widgets taken down and built again |
| 135, 124, 94 ms, walk-led | the frame thread waiting in `CellSupply::waitForOne`, 111 ms of 135: a tour runs settled, as its stated step makes it (`RtxRenderer::configureResources`), so its walk waits for its nearest cell, which no player does |
| 4.2 s, 2.4 s, 273 ms | `AsyncNavMeshUpdater::waitUntilJobsDoneForNotPresentTiles` after a cell loads, upstream's loading-screen wait (`wait until min distance to player = 5`). Not met in the run without context-switch sampling, which slows the threads that build the mesh |

One 215 ms frame waited 181 ms with no frame of the fork's on its stack, preempted while runnable. It is unexplained,
and is read as the sampling's own.

## What follows

- **Upstream's reference lookups scan every cell store loaded**, and none is unloaded (3,065 after a Repack tour's
  first legs). Proposed to the user with this profile, not changed: `UPSTREAM.md` requires a yes first.
- **Device memory made on the frame thread** became item 19 of the plan.
- **A tour's settled walk waits** where the game does not. Whether tours run unsettled is to be decided by an A/B of
  worst frames and outruns on the handheld.

## Levels by distance, measured (item 16)

Bytes standing whole above the level their nearest reader asks (`TexturesAboveWant`), at `66aa444291`:

| machine | content | worst stop | middle stop | every stop together |
|---|---|---|---|---|
| desk | PBR | 451 of 1,948 MiB, 23.2%, first land leg | 5.4% | 6.1% |
| handheld | PBR | 451 of 1,948 MiB, 23.2%, first land leg | 5.5% | 6.1% |
| handheld | Repack | 78 of 458 MiB, 17.0%, a land leg | 6.9% | 6.7% |

The Repack tour was cut at stop 135 of 155 when the session that started it was lost; no crash was recorded. The desk's
rest held about 7 GiB at the PBR tour's peak, beside the 2 GiB named.
