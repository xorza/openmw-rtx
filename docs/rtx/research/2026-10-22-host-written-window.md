# The host-written window at the world's room — 2026-10-22

RTX 4090 Laptop, driver 615.78.08, Linux, release build at `4ec5fe3a1a`. Every buffer made as
`BufferKind::HostWritten` was logged as it was made and buried, and the set standing at the peak
summed by name. Three runs: `bench --views=vivec,seyda-neen-ship,balmora-mages-guild`, the
`streaming` suite (`island-crossing`, flown across the island), and `shot --doll=fargoth` at
`seyda-neen-ship`, which traces the inventory's doll in a scene of its own.

An RTX 20 card has no resizable BAR: its host-written window is a 256 MiB heap, of which the driver's
budget offers about 246 MiB.

| run | live at the peak | blocks reserved |
|---|---|---|
| three places | 120.9 MiB | 184 MiB |
| the crossing | 123.4 MiB | 184 MiB |
| the doll | 123.4 MiB | 184 MiB |

What stands at the peak of the crossing, by buffer:

| buffer | MiB | buffers |
|---|---|---|
| instance rows (`GpuInstance`, 160 B × 2^18, a copy a frame slot) | 80.00 | 4 |
| top-level rows (64 B × 2^18, a copy a frame slot) | 32.00 | 4 |
| light cells | 4.71 | 3 |
| light list | 4.68 | 4 |
| materials | 1.75 | 4 |
| everything else: poses, texels, lights, sprites, starts, ripples, FSR's constants, the GUI's vertices, the launches' tables | 0.26 | 44 |

The four buffers of each table are the world's two copies and the doll's two, which are opened at
what the doll holds. The light tables are the only rows that grow with the place: 2.8 MiB at the
three places and 4.7 MiB on the crossing.

**Not in reach at the room.** The tables take half the window and the blocks three quarters, so no
second path for the per-frame rows is made. A growth past the room (`sWorldRoom`, three times the
largest place's placements) would make the instance and top-level rows again at twice the size,
224 MiB for their copies alone, beside the copies the frames in flight still read: that window does
not hold it, and the renderer would end there. `sceneroom.hpp` states the window it was held to.
