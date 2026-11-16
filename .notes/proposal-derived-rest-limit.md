# Proposal: the rest limit derived from the device's budget

Replaces the measured limit on what rests (`SceneUploader::restLimitOf`, `min(2 GiB, budget / 4)`) with one derived
from the driver's budget: live content takes what it needs, and the content nothing names keeps a share of what is
left. Facts and measurements: `docs/research/2026-11-16-rest-reuse-and-device-budgets.md`. When it lands, the
decisions fold into `docs/architecture.md`, "One residency policy for the content", replacing "The limit is
measured", and this file is deleted.

## Decisions

- **The limit is derived from the budget, by the user's decision**: what rests may stand 9/10 of the budget less what
  is in use, where in use is the driver's usage of the video heap less what rests, read at each hand-over. The
  driver's budget is already what is free plus what the process holds (RADV computes it so; Windows's is the OS's), and
  VMA reads it every frame, so nothing is polled apart. As VMA's guide and the D3D12 residency model treat a card, and
  as Dawn evicts least recently used only past its budget. What rests is taken off the usage because the usage counts
  it, and a cache that grew would otherwise shrink its own limit. Refused: the measured `min(2 GiB, budget / 4)`,
  which missed 11.6% of the PBR tour's reuse on the desk and held the handheld to 1.5 GiB; a derived limit clamped by
  a measured ceiling, as Unreal clamps its pool percentage, a figure the content decides (the repack's reuse is whole at
  4 GiB, the PBR replacers' at 8).
- **The tenth left is other programs' share, by the user's decision**, and the room an arrival needs is kept apart, as
  now (the largest arrival so far and what waits, evicted ahead of a hand-over). Windows lowers a process's budget when
  another needs memory; Linux tells no one, so the tenth is what a browser or the compositor finds free before our next
  reading sees their usage. Refused: Dawn's 95%, less for them; the arrival room alone, which leaves them nothing.
- **Live content is bounded by the budget alone**, as now: a texture past it stands a level smaller or the stand-in, a
  structure waits and then is left out. Past the budget a process is frozen or refused on Windows, refused on NVIDIA's
  Linux driver, and pages into system memory on the handheld, so the budget is the line and not a limit of our own.
- **The handheld's two caches compete, by the user's decision**: the host's caches take what the system leaves less
  1 GiB, once a second, and already yield to an integrated device's memory by their own decision ("The budget is
  derived"); the device's rest grows at an arrival and the host's caches shrink at their next update. Content that
  rests on the device keeps no host copy, so a hit there saves the read and the upload both. Refused: a fixed split of
  the free memory on a unified device, a figure no measurement sets; one least-recently-used order across host and
  device, a large change to upstream's resource caches.
- **A budget that drops is followed at a bounded pace, by the user's decision**: the room an arrival needs is freed at
  once, since that hand-over needs it, and what stands past the limit beside it goes at most one step a hand-over,
  the most bytes any one hand-over has handed the device, a rebuild's included, so that a rest a rebuild left is paced
  as well. Microsoft's guidance is to destroy unused objects roughly once a frame. Refused: all at once, one long frame
  per drop, worst on switching back into the game on Windows, whose budget "fluctuates dramatically" then.
- **Not now: lowering a resting allocation's priority**, by the user's decision (`VK_EXT_pageable_device_local_memory`,
  Vulkan's counterpart of D3D12's residency priorities and Metal's purgeable state). RADV does not implement it, a
  priority is an allocation's and so reaches no content placed in a shared block, and the tenth already leaves room.
  Revisited if a tour shows another program starved.

## Shape

- `ArrivalRoom` gains the driver's usage of the video heap, every slot's, read with the budget in one call of the
  allocator (`MemoryAllocator::getVideoBudget` becomes a read of both).
- `SceneUploader::restLimitOf(room, resting)` answers `(budget - inUse) * 9 / 10`, nought where nothing is left, with
  `inUse = usage - resting`; `sRestMost` and `sRestShare` go, and the tenth is a named ratio saying whose share it is.
- `SceneDesc::evictResting` takes the step beside the room: it frees the room whole, then the oldest past the limit
  until a step is freed. The uploader keeps the largest hand-over beside the largest arrival.
- `RestReuse` stays: it is what says whether the share was worth taking.

## Steps

1. The allocator's read of budget and usage; `ArrivalRoom::mUsage` filled by the Vulkan renderer and the test
   renderer.
2. The derived limit, with exact tests: a budget of 6,000 bytes, a usage of 4,000 and 1,000 resting stand
   `(6,000 - 3,000) * 9 / 10 = 2,700`; a usage past the budget stands nought; the resting bytes shown to matter.
3. The paced eviction, with a test: 5,000 bytes past the limit and a step of 1,000 free in five hand-overs, the room
   an arrival needs at once in the first.
4. `docs/architecture.md` and the comments that name the measured limit.
5. The tours on the three machines, reading the resting bytes against the budget, the slow frames, and the rest reuse.
