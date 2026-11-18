# Proposal: one owner of the device's memory, which gives memory back

The handheld's Repack rooms run at a legs' p99 median of 51 ms on the branch against 21 ms at the baseline, and only
after the tour's land legs: the device then holds more than its budget and never comes back under it
(`docs/research/2026-11-18-handheld-rooms-and-device-memory.md`). Four open issues are this one shape — the rooms'
frames, the three structure blocks refused, the stand-in at Seyda Neen, and the Windows machine's integrated GPU whose
memory is the process's own — and four fixes aimed at one symptom each were built and declined (items 14.6 and 21 of
the last plan, freeing the oldest resting meshes, moving the textures together). This plan replaces the mechanisms that
each guard one use with one owner that holds the device to one target and gives memory back when it is over.

Written for `/execute-plan`, its decisions made (**Approved**): items in order, each a commit, `./omw gate` at the
end, the merge into master after it. When it lands, its decisions fold into `docs/architecture.md`
("Device and memory") and this file is deleted.

## What the tours show

- **Held memory only grows.** In all three whole tours the device's blocks rise over the land legs and stay; on the
  branch they reach 5928 MiB by stop 37 and the budget falls under them from there to the end, 119 stops of 155. The
  baseline held 5081 MiB under a budget of 6144 throughout.
- **The budget moves and the held memory does not.** On a unified device the budget is the lesser of the driver's and
  what the system leaves the process (`MemoryAllocator::systemBoundOf`), read once a second; the host's side grows over
  a tour, the bound falls from 6144 to 5346 MiB, and nothing brings the device under it.
- **What sits in the blocks is not all content.** In the rooms the structures' blocks stand at 2184 MiB with 711 MiB of
  them free, 1049 by the tour's end; the textures' pools hold 229 MiB free. A block goes back only when its last range
  leaves (`StructureStorage::give`, VMA's pools), so free runs are held by the system as if they were used.
- **The branch holds more texture than the baseline**: 2131 MiB in the rooms against 1432. *Inferred*, not measured:
  the branch stands a texture at the level its nearest user asks, where the baseline stood it at what the room
  allowed.
- **Held under 4 GiB, the rooms run at 27.9 ms.** The cap brings most of it back; the device then sits at its cap from
  stop 61 on, its own bound under the cap, and still over it at 91 stops.
- **The swap is not the cause**: the baseline swapped 3.4 GiB, the branch 1.6, and the baseline's rooms were the fast
  ones.

## Why the mechanisms in place cannot fix it

Each guards one use at the moment it takes memory, and none lowers what is held:

- **Content's ceilings refuse new takes** (`MemoryAllocator::ceilingOf`) and leave what stands alone.
- **The rest limit evicts what rests** (`SceneUploader::restLimitOf`, `RestLimit`): ranges inside blocks, which stay.
- **Textures move down for an arrival or a reserve that grew** (`TextureArray::makeRoom`), into new ranges; the old
  ones free inside their blocks.
- **Wants free a resting row that fits** (`SceneDesc::evictFor`): again a range.
- **A structure's room is reused only by a structure** (`MemoryAllocator::noteStructureFree`), and its block only goes
  when empty.

So every path frees ranges, and the system backs blocks. Item 21 (VMA's incremental defragmentation of the texture
pool) was the one path that gave blocks back, and it was declined because the caches took the freed memory and the
device's bound fell after it: two owners read the same "available" figure and race for it.

## How others do it

- **D3D12's residency** keeps resident objects in least-recently-used order and trims to the budget before each
  submission, and the OS can ask an application over budget to trim a stated number of bytes
  ([residency](https://learn.microsoft.com/en-ie/Windows/Win32/direct3d12/residency),
  [trim notifications](https://microsoft.github.io/DirectX-Specs/d3d/D3D12_PeriodicTrimNotifications.html)).
- **Dawn's residency manager** holds its budget to 95% of the OS's less an external reservation, and evicts until a new
  allocation fits
  ([ResidencyManagerD3D12.cpp](https://dawn.googlesource.com/dawn/+/757d153d54fda786fdfd882ba6d8d5ee44efab5b/src/dawn_native/d3d12/ResidencyManagerD3D12.cpp)).
- **Unreal's texture streamer** stands textures in a pool of fixed size and, over it, drops each texture's top levels
  by a bias up to `MipBias`; just over the pool, it streams in and out without settling
  ([configuration](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-configuration-in-unreal-engine),
  [the thrash](https://forums.unrealengine.com/t/unstable-texture-and-mesh-streaming-with-r-streaming-mipbias-0/2687355)).
- **VMA** leaves staying within the budget to its user: `vmaGetHeapBudgets` once a frame, refusing non-essential
  allocations past it, and defragmentation to empty blocks
  ([staying within budget](https://skia.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/+/4b01ef07ef0c710f99968b6395fea8ce8b6b16a6/docs/html/staying_within_budget.html)).

All four hold a target that what is held is driven to, not only a ceiling that new memory is refused at.

## Decisions proposed

- **One owner of the device's memory, `DeviceResidency`, holding it to one target.** It reads the budget, counts what
  the device holds by blocks, what is free in them and by use, and at each hand-over sets what to give back. Refused:
  a ceiling per use as the only guard, which is what never comes back under a falling budget; a residency thread,
  since every step it takes is bounded and runs on the hand-over that already reads the budget.
- **Held is counted as the system backs it: blocks.** A free run in a block is held until the block goes. Refused:
  ranges as held, which read 711 MiB of structure storage as room while the system held it.
- **Over the target, give back a step a hand-over, cheapest first** — the step is the frame's share the texture moves
  already take (`TextureArray::makeRoom`), so no frame absorbs a compaction:
  1. what rests, oldest first (the rest queue, as now);
  2. what stands above its want, a level down (as now);
  3. blocks left empty, to the device;
  4. **the least-filled block emptied**: a structure copied into another block's free run, as the tight copy already
     is (`BottomLevelStore`, its placement rewritten by `SceneAcceleration::placeCompacted`), or a texture copied at
     its level into another range, then the block goes;
  5. **a bias below every want**, a level more each time the steps above fall short, as Unreal's `MipBias`;
  6. refusing an arrival, the stand-in or a mesh left out, last.
  Refused: eviction alone (the branch's free runs are not resting rows); defragmentation of every pool at once (item
  21's first form); refusing first.
- **A band, not a line**: give back above the target, grow again only below it by a step's margin, so a want just over
  the pool does not stream in and out as Unreal's does. Refused: one threshold.
- **On a unified device, one split of the process's memory between the device and the host's caches.** The device's
  target is set from the memory the system leaves the process and moves slowly; the caches' budget is what that leaves
  less the device's *target*, not its held memory, so a block the device gives back stays the device's to take again.
  This is what sank item 21: both read what the system leaves, and a block given back was the caches' next second.
  Refused: the device's bound read each second against the caches', as now; a fixed split, which a host that grows in
  a long session outruns.
- **The mechanisms the owner replaces go**: the rest limit and its step (`RestLimit`, `restLimitOf`), `evictForRoom`,
  the reserve's own call to `makeRoom`, and content's "over" as a separate query (`getContentOver`). Wants and waits
  stay as what an arrival asks of the owner. Refused: the owner beside them, two routes to one result.

## Approved

Decided by the user, 2026-11-18:

1. **The unified split follows smoothly**: the device's target is an exponential average of what the split gives it,
   over about a minute, both ways, so a host's spike costs the device nothing and a lasting growth takes its room back
   within the minute. Refused: down at once and up slowly; a fixed share at the start.
2. **Textures move between blocks by the texture array's own copy**, at their level, the least-filled block first, a
   frame's share of copies a hand-over, the block given back once empty. Refused: VMA's defragmentation, which chooses
   what moves outside that bound; structures alone.
3. **The bias below every want comes before a refusal**, a level at a time while the steps before it fall short, never
   under 64 texels a side, and off again a level a step once the device is under the band. Refused: refusing first.

## Open measurements, before the items they decide

- **M1, that the device over its budget is what makes the host wait.** Read the handheld's amdgpu counters across a
  tour (its per-process memory in `fdinfo`, and the driver's count of evictions), at each stop with held and budget,
  and hold the slow rooms' frames against them. Decides whether the target is the budget itself or under it.
- **M2, what the Windows meter calls swapped.** Its swapped figure is private memory committed and not in the working
  set, which the integrated GPU's pages are whether or not they reached the page file. Held against the whole Repack
  flown under 4 GiB (flying at the time of writing). Decides whether the Windows machine pages at all.
- **M3, what is left of the rooms under the cap**, 27.9 ms against 21: what the capped flight refused and waited for,
  and what moved, by stop.

## Items

1. **Measure M1 and M3** in the harness: the device's held, budget, free runs by use and what was given back, a line a
   stop and a column in `tour.json`; on Linux the amdgpu `fdinfo` figures beside them. Flown on the handheld's whole
   Repack (`--every=1`). Test: the record's fields, from a fake renderer's figures.
2. **Settle M2**: the Windows capped tour read against the uncapped one; the meter's swapped figure split from the
   device's pages where Windows says so, or named for what it is.
3. **The unified split**: one owner of what the system leaves the process, from which the device's target and the
   caches' budget are both derived (approved 1). Test: the split's arithmetic, exact values, and that a block the device
   gives back raises no cache's budget.
4. **`DeviceResidency`**: the target, held by blocks, the band, and the ladder's first three steps (rests, above want,
   empty blocks), replacing the rest limit, `evictForRoom` and the reserve's `makeRoom`. Test: the ladder as a function
   of the figures it reads, each step's bytes exact; the hand-over's allocation count held at nought.
5. **Structures out of the least-filled block**: the copy the tight copy uses, into another block's free run, a step a
   hand-over, the block given back when empty. Test on the device: a block emptied by moves frees, the instance rows
   name the new addresses, and `omw repeat` agrees frame for frame.
6. **Textures out of the least-filled block** (approved 2), the same way. Test on the device: the moved image samples
   the same texels, its slot's descriptor names it, and the block goes.
7. **The bias below every want** (approved 3), the last step before a refusal, coming off a level a step once under the
   band. Test: the bias's steps against a scripted budget, and that it never takes a texture under 64 texels.
8. **Close**: the handheld's Repack and PBR and the Windows machine's Repack flown whole (`--every=1`), held against
   the baseline and against `docs/research/2026-11-18-handheld-rooms-and-device-memory.md`. They pass where the rooms'
   legs' p99 median is within the baseline's spread, no flight refuses a structure block, the stand-in at Seyda Neen is
   gone, and the device is under its target at every stop past the first land leg. Then the issues are rewritten, the
   figures go into a dated research note, the gate and CI run, `overnight` merges into master, and this file goes.
