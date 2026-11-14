# The scene's tables at three contents — 2026-11-14

The desk: a high-end NVIDIA laptop GPU. `./omw release scene --view=seyda-neen-ship` on the review branch after
its sixth batch, on vanilla, on the large mod list and on the PBR replacers, with a scene report widened for the
run by the figures below, and the run allocator counting its holes and timing its `allocate`. The widening was
not kept.

| | vanilla | large list | PBR |
|---|---|---|---|
| placements | 64,181 | 38,698 | 68,107 |
| vertices | 1,890,789 | 2,969,008 | 1,903,488 |
| vertex and index bytes | 100.1 MiB | 153.3 MiB | 100.8 MiB |
| vertices with a tangent word not nought | 0 | 5090 | 492,856 |
| ground mask weights, as floats | 2,187,152 (8.3 MiB) | 3,203,276 (12.2 MiB) | 2,187,152 (8.3 MiB) |
| vertex holes, index holes at the end | 3, 8 | 7, 12 | 6, 8 |
| most holes of one allocator during the load | 1132 to 1146 | 1516 | 1117 |
| vertex-run `allocate` calls, their time, the longest | 5096, 0.105 ms, 0.3 µs | 6702, 0.178 ms, 0.9 µs | |

The most holes were the vertex runs' (a block of 262,144) on the large list and in one vanilla run, the index
runs' (a block of 1,048,576) in another. The ship stands 352 ground cells on every content.
