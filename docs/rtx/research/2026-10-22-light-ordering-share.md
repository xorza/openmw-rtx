# What ordering the lights costs a frame — 2026-10-22

RTX 4090 Laptop, i9-13980HX, Linux, release build at `bd8f9f8526`, `./omw profile` (`task-clock` at
6 kHz).

| run | samples | `SceneDesc::orderLights` | `LightGrid::build`, `rebuild` |
|---|---|---|---|
| `balmora-storm-night`, `balmora-fog-night`, `vivec`, `balmora-mages-guild`, 1200 frames each | 151 K over 25.2 s | 0.32%, about 17 µs a frame | no sample |
| `streaming` (`island-crossing`, 19 crossings, cells arriving with their lamps) | 26 K over 4.4 s | no sample | no sample |

The sort runs on every walk, over a few dozen lights (48 in the storm's cell), and the grid is
rebuilt on every placement; neither reaches a worst frame, which on the crossing is the engine's
update (`2026-10-22-crossing-worst-frames.md`). The lights stay sorted by position, and the grid
keyed by their sorted index.
