# What the crossing's worst frames are made of — 2026-10-22

RTX 4090 Laptop, i9-13980HX, driver 615.78.08, Linux, release build at `ea358ee70c`. The
`streaming` suite (`island-crossing`, flown across the island fast enough that cells arrive under
frames in flight, `settled = false`), twice with `--frame-times`, after a warm-up leg, and once under
`./omw profile` (`task-clock` at 6 kHz, 26 K samples over 4.39 s).

| | run 1 | run 2 |
|---|---|---|
| frame median / p99 / worst | 4.80 / 14.75 / 24.70 ms | 4.79 / 15.84 / 23.96 ms |
| `update` p99 / worst | 11.02 / 21.00 ms | 11.36 / 20.42 ms |
| `upload` p99 / worst | 2.06 / 3.01 ms | 1.98 / 2.89 ms |
| `walk` p99 / worst | 2.52 / 3.94 ms | 2.61 / 4.32 ms |
| arrival frames, worst | 367, 16.9 ms | 367, 17.4 ms |

The three worst frames of each run are the engine's update (`MWWorld::Scene::loadCell`, the
physics and the navigator's jobs on the main thread): 21.0, 12.5 and 12.2 ms of 24.7, 17.9 and 16.9,
with no mesh arriving on the worst of them.

What the profile gives the renderer's arrival and its frees, over the whole run:

| | share of samples | over the run |
|---|---|---|
| `SceneUploader::hand` (every placement and arrival) | 4.85% | 213 ms |
| `DeviceScene::extend` (the arrivals' recording) | 0.36% | 15.8 ms |
| `BottomLevelStore::build` | 0.04% | 1.8 ms |
| `Graveyard::freeThrough` (every destroy) | 0.05% | 2.2 ms |
| `vmaAllocateMemory*` (every new block or range) | 0.02% | 0.9 ms |

**None of them shows in a worst frame.** A cell's arrival rides one submit and costs the frame at
most the 3 ms of `upload`; the graveyard's destroys and VMA's new blocks are milliseconds over the
whole crossing. An arrival budget, a destroyer thread and a block made ahead by a worker each answer
a cost the worst frame does not have here, so none is made.
