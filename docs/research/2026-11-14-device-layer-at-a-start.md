# The device layer at a start — 2026-11-14

The desk: a high-end NVIDIA laptop GPU. `./omw release scene --view=seyda-neen-ship` on the review branch in its
eighth batch, on vanilla, on the large mod list and on the PBR replacers, with the release harness widened for
the runs by a log line and counters that were not kept.

## The world's one submit

`VulkanRenderer::setScene` stages the whole world through one batch and flushes it once, then trims the ring.

| | vanilla | large list | PBR |
|---|---|---|---|
| the flush, submit and wait | 2.0 ms | 22.6 ms | 103.6 ms |
| the staging ring at the flush | 56 MiB | 299 MiB | 1435 MiB |

## The content's allocations

`MemoryAllocator`'s content allocations at the same stop: 1526 in blocks, 369.3 MiB, on vanilla, and 3464, 3655.2
MiB, on PBR. None was an allocation of its own: no content resource is past half of a 64 MiB block.

## The float decorations

The release build's 74 shader modules hold 3.39 MiB of SPIR-V, of which the per-result `NoContraction`
decorations the pinning writes are 0.40 MiB.

## The backend's scene after the load

At the same stop, after `setScene`'s flush:

| | vanilla | large list | PBR |
|---|---|---|---|
| the bottom levels' build scratch (`BottomLevelStore::getScratchBytes`) | 2.3 MiB | 3.0 MiB | 2.5 MiB |
| the normal spreads' means (`TextureArrival::getMeansBytes`) | 0 | 26.7 MiB | 85.3 MiB |
| every bottom level built with `ALLOW_DATA_ACCESS`, as the driver sizes it | 133.0 MiB | 232.3 MiB | |
| the same without the flag | 133.0 MiB | 232.3 MiB | |

PBR's means are past `sSpreadMeansRoom`'s 64 MiB: one map whose means alone are larger than the room, a group of its
own. The two size columns are the driver's answer to `vkGetAccelerationStructureBuildSizesKHR` for the same builds,
asked twice, on the first 4500 and 6000 meshes.
