# The trace's stages on a low-end AMD integrated GPU — 2026-10-21

A low-end AMD integrated GPU (RDNA 2), AMD's Windows driver 26.8.1, Windows 11. The Fullrest Repack at
`seyda-neen-ship`, release, `bench --views=seyda-neen-ship --frames=5 --warmup=0 --upscale=off` at 480x270 unless
named; zones are medians in ms.

## One kernel carrying every walk spilled

The walk of `2026-10-19-amd-windows-trace-compile-memory.md` as one compute dispatch: one frame took 650 ms of trace and
one interface picture 8.8 s, which at Windows' default two-second `TdrDelay` reset the device and froze the machine.

- **The traversal was not the cost**: 20 turns of the loop a pixel, 6 candidates a ray at most; tracing every ray twice
  moved the frame by nothing, and one turn with no ray drew it in 30 ms.
- **The loop carried about 800 words a lane through 256 registers**, the walks' records, and the driver's code was its
  spills: 33,139 scratch stores and 27,811 loads against 5,020 multiplies, 67,584 bytes of scratch a lane.

## Each stage's own loops

Each change on top of the ones before; the frame in ms at 480x270:

| change | frame |
|---|---|
| the stages as first written, each phase of a stage in one loop with the others | 412 |
| `direct` and `bounce` find the solid in a loop of their own | — |
| `panes` peels only where a layer stands | 80 |
| the trace's peel as straight code, shading after the loop | 59 |
| a committed triangle's corners off the scene's own vertices, every triangle opaque | 57.6 |
| `panes` peels in straight code, each layer's light and medium in loops of their own | 51.7 |
| `direct` and `bounce` resolve the hit the trace stores, with no traversal | 46.7 |
| every tuple compiled behind the full one, the frame's own taken once made | 45.0 |

At 1920x1080 (1280x720 traced) the frame went from 1,136 ms at a native trace before the straight peel to 198 ms with
the hit stored. The trace's spill was its loop's back edge: the driver stored 276 registers at the head of the peel's
loop and loaded them through the body; leaving the loop with the hit and shading after it removed all of it.

## What the specialized tuples and the corners cost

- **Specialized tuples** against the full kernel: `bounce` 16.9 against 18.6 ms, `direct` 7.6 against 8.5; every tuple
  compiled in 61 s at a first start where the full kernels take about 10, so the tuples compile behind the first frames.
- **A committed triangle's corners as this driver fetches them are wrong**: read off the scene's own vertices
  (`committedHit`), they draw what the candidates drew. A second query over a slab about the hit to find them cost
  near what the whole ray did.
- **`repeat`** on this card: every pair identical.
