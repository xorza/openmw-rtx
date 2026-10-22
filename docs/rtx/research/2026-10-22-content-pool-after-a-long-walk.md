# The content pool after a long walk — 2026-10-22

RTX 4090 Laptop, driver 615.78.08, Linux, release build at `6a87d9465d`. The 17 bench places
(`default`, `exteriors`, `skies` and `interiors`) run one after another in one process, each a
stop of 20 seconds, and the `streaming` suite's crossing of the island in a process of its own. The
heap's figures are VMA's, read after each place (`MemoryReport`); the structures' are the scene's
(`SceneStats`). Records in `.notes/perf/batch8/`.

The device heap, reserved against live, after each place, in MiB:

| place | reserved | live | reserved past live |
|---|---|---|---|
| seyda-neen-ship | 1536.3 | 1394.9 | 141.4 |
| seyda-neen-shore | 1536.3 | 1456.4 | 79.9 |
| balmora | 1600.3 | 1463.8 | 136.5 |
| ald-ruhn | 1664.3 | 1496.7 | 167.6 |
| dagon-fel | 1664.3 | 1415.8 | 248.5 |
| andrano-tomb, the last | 1664.3 | 1472.1 | 192.2 |
| island-crossing, its own process | 1664.3 | 1513.9 | 150.4 |

The structures' blocks against what the structures hold:

| place | blocks | structures | blocks past them |
|---|---|---|---|
| seyda-neen-ship | 148.4 | 139.0 | 9.4 |
| ald-ruhn | 164.4 | 155.7 | 8.7 |
| sadrith-mora | 164.4 | 136.0 | 28.4 |
| dagon-fel | 148.4 | 121.9 | 26.5 |
| andrano-tomb | 156.4 | 151.1 | 5.3 |
| island-crossing | 171.4 | 158.1 | 13.3 |

**The pool stands at its high-water mark and does not creep.** Reserved memory rises to the largest
place's reach by the seventh place, 1664 MiB, and holds there over the ten places after it, the
largest gap 248 MiB (15% of the reservation) where a small place followed a large one; the
structures' blocks follow the place down as well as up. A defragmentation or a structure relocation
(`vkCmdCopyAccelerationStructureKHR`) would give back at most the gap of the moment, which the next
large place takes again. Neither is made.
