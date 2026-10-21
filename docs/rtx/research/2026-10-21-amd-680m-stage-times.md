# The trace's stages on a Radeon 680M — 2026-10-21

Radeon 680M (RDNA 2, integrated), Adrenalin 26.8.1, Windows 11. M[FR], `seyda-neen-ship`, release
build, `./omw release bench --views=seyda-neen-ship --frames=5 --warmup=0 --upscale=off` at
480x270 unless named; zones are medians in ms. Spill is the full tuple's
(`RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`), bytes of scratch a lane at 256
registers.

## Before the stages: the walk's records spill (2026-10-20)

The walk of `2026-10-19-amd-windows-trace-compile-memory.md`, "The walk: one traversal site", as a compute dispatch; `TdrDelay` raised to 30 s; `seyda-neen-ship` in M[FR] at 80x45,
upscale off, the debug flavour (its shaders are the release build's). 38658 instances.

- **One frame took 650 ms of trace and one interface picture 8.8 s.** The first frame of a real
  scene draws the local map and the doll in one deferred batch, which at the default two seconds
  was the reset (`VIDEO_ENGINE_TIMEOUT_DETECTED`, code 141, in `LiveKernelReports\WATCHDOG`) that
  froze the machine over RDP.
- **The traversal is not the cost.** A frame traced 18 300 rays (7 340 resolved) in 71 500 turns
  of the loop, 20 a pixel and 72 at most, and its candidate loops met 14 300 candidates, 6 at most
  for one ray. Every ray traced twice moved the frame by nothing (651 against 655 ms); the
  corners read as candidates (`CANDIDATE_CORNERS`) or committed moved it by 30 ms. One turn and
  no ray drew the frame in 30 ms.
- **Every arm costs alike, and the back edge most.** The real-time clock summed by lanes over a
  frame put 1.4 % in the traversal, 23 % between one turn's last arm and the next turn's query,
  where nothing is computed, and 8–16 % in each of the arms, whatever the arm does.
- **The loop carries about 800 words a lane through 256 registers.** The records are 734 words
  (`GatherRun` 118, `SolidRun` 114, `PixelRun` 109, `WaterRun` 103, `EyeRun` 96, `VisitRun` 69,
  `WaterRayRun` 52, `ColumnRun` 43, `SkyPassageRun` 30), with `Surface` 44 and `RayAsk` 18 beside
  them; the module's loop header holds 399 phis. The SPIR-V keeps them in registers: it has three
  function variables, none a record.
- **The driver's code is its spills.** `vkGetPipelineExecutableInternalRepresentationsKHR` gave
  236 538 lines of wave32 code holding 33 139 `scratch_store_dword`, 27 811 `scratch_load_dword`,
  69 241 `v_mov_b32` and 60 924 `v_add_nc_i32` — the scratch addresses — against 5 020
  `v_mul_f32`; and 67 584 bytes of scratch, a lane's (addresses reach 0x37d8 and past), for some
  3.2 KB of state.

## Each stage's own loops

Each row adds its change to every row above it, and an empty cell is a zone that row did not
measure.

| Change | frame | trace | direct | panes | bounce | Spill after |
|---|---|---|---|---|---|---|
| The stages as first written, each phase of a stage in one loop with the others | 412 | 92 | 12 | 31 | 236 | trace 3 280, direct 256, panes 9 856, bounce 3 584 |
| `direct` and `bounce` find the solid in a loop of their own (`foundSolidOf`); the bounce's ray apart from its landing's visit | | | 9 | | 19 | direct 0, bounce 272 |
| The trace's water walk in a loop of its own, and no surface carried between turns | | 24 | | | | trace 2 304 |
| `panes` peels only where a layer stands (`STAGE_LAYERED`) | 80 | | | 9 | | |
| The trace's peel as straight code, shading after the loop | 59 | 6.4 | 9.9 | 8.5 | 20.6 | trace 0, panes 8 336 |
| A committed triangle's corners off the scene's own vertices, every triangle opaque | 57.6 | 5.7 | 8.5 | 8.7 | 18.6 | bounce 160, media 0, panes 8 352 |
| A split pixel's lift blended by `bounce` where no layer stands, and not by `panes` | 53.9 | 5.8 | 8.6 | 4.3 | 18.5 | |
| `panes` peels in straight code, each layer's light and medium in loops of their own | 51.7 | 5.8 | 8.6 | 0.34 | 18.5 | panes 192 |
| `direct` and `bounce` resolve the hit the trace stores, with no traversal | 46.7 | 5.8 | 3.9 | | 13.3 | bounce 176, panes 208: the transforms read off the instance row |
| Every tuple compiled behind the full one, and the frame's own taken once made | 45.0 | 5.7 | 3.4 | | 11.4 | |

At 1920x1080 with the default upscale (1280x720 traced): the straight peel's row 307 ms, `bounce`
130, `direct` 53, `panes` 34, `trace` 34, `tlas` 6.5; the last row 251 ms (4.0 fps), `bounce` 118,
`direct` 46.5, `trace` 30.8, `panes` under 1; with the hit stored, 198 ms (5.0 fps), `bounce` 90,
`direct` 21.9, `trace` 30.8. The row before the straight peel at a native
1920x1080 trace: frame 1136 ms, `trace` 576, `bounce` 281, `direct` 107, `panes` 79.

**The trace's spill was its loop's back edge.** The driver's code (`VK_KHR_pipeline_executable_properties`,
internal representations) stored 276 registers to scratch at the head of the peel's loop, the
records of the eye and the peel with an answer each, and loaded them through the body. Passing an
idle record to `stepEye` as a local, removing the sprites, the water or the medium each moved the
spill by under 300 bytes; leaving the loop with the hit and shading after it removed all of it.

## What a stage's rays cost

- **`panes` over the tiles where a pixel splits and no layer stands**, which it only blended the
  lift of: `panes` 4.2 ms with those tiles left out of its list against 8.7 with them, at 256
  registers and 8 352 bytes of scratch a lane whatever a tile holds.
- **The specialized tuples on AMD** (`TraceCompile::Ordinary` forced): `bounce` 16.9 ms against
  18.6 for the full kernel, `direct` 7.6 against 8.5, `panes` 7.6 against 8.7, `trace` 5.8 alike;
  every tuple compiled in 61 s at a first start, where the full kernels alone take about 10.
- **Every tuple behind the full one**, over half the hands: the 15 tuples' 57 kernels compiled in
  59 s after the full tuple's, from the driver's cache, in under a second; the cold peak at half
  the hands is not measured, the cache holding every tuple since.
- **`./omw release repeat`** on the 680M: two pairs at 480x270 and six at the default size, each
  identical; the seventh stopped at the run's 25-minute bound.

- **Finding the solid again** (`foundSolidOf`) alone, the rest of `bounce` cut: `bounce` 6.4 ms,
  as long as the whole trace kernel; 5.5 ms once the corners came off the scene's vertices, and
  28.7 ms at the default size (1280x720 traced) of a 251 ms frame. Without the resolve, the
  surface left as the hit's point alone: `bounce` 5.65 ms and `direct` 7.1 against 8.7, at
  480x270.
- **Every triangle a candidate on a resolved ray** (`CANDIDATE_CORNERS`). Traced with the
  triangles opaque and the committed corners fetched, which this driver reads wrong: frame 51 ms
  against 59, `bounce` 10.3 against 20.6, `direct` 7.6 against 9.9, `trace` 5.7 against 6.4 — low,
  since a bounce from a wrong plane leaves from the wrong place. Opaque, with the corners read off
  the scene's own vertices (`committedHit`), which draws what the candidates drew: frame 57.6,
  `bounce` 18.6, `direct` 8.5, `trace` 5.7.
- **The committed corners read off a second query over a slab about the hit**, a thousandth of the
  distance either side and every triangle in it a candidate: frame 77 ms, `bounce` 28.1. The two
  queries in one loop with the slab never taken: `bounce` 12.8. A traversal over a slab costs
  near what the whole ray costs; not kept.
