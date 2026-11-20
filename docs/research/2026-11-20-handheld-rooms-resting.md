# The handheld's rooms after the land, against the rooms alone — 2026-11-20

The open issue of `.notes/ISSUES.md`: the handheld's Fullrest Repack rooms run slower after the land legs than flown
alone. Release builds at master `740f9accc2`, `./omw release remote <handheld> tour --replace=config --config=<Repack
profile> --expected=mfr`, three flights back to back: the sampled rooms alone (`--legs=rooms`), the sampled tour whole
(the land legs, then the same rooms), and the tour whole again under a probe that let go of every resting mesh and
texture at each change of worldspace (`SceneDesc::releaseResting` from `RtxRenderer::notifyWorldspaceChanged`). Medians
over the 25 rooms the three share; a host row reads its legs' median / p99.

| flight | frame p99 | first four rooms' p99 | `place` | `walk` | `wait` | main thread, M cycles a frame | resting vertices | live slots | device held |
|---|---|---|---|---|---|---|---|---|---|
| rooms alone | 22.5 ms | 23.5 | 0.78 / 2.3 | 2.05 / 2.5 | 6.56 / 9.6 | 26.5 | 1.10 M | 5976 | 2604 MiB |
| after the land | 29.5 ms | 33.8 | 1.86 / 13.0 | 3.04 / 3.6 | 0.03 / 4.9 | 43.3 | 6.74 M | 21218 | 4727 MiB |
| after the land, resting let go | 24.4 ms | 25.6 | 1.11 / 2.4 | 2.33 / 3.0 | 5.67 / 10.0 | 27.0 | 0 | 3142 | 4151 MiB |

- **What the land leaves is the resting meshes and textures**: let go of at the rooms' doors, the rooms run within 2
  ms of alone, the hand-over's p99 falls from 13.0 to 2.4 ms and the main thread's cycles a frame from 43.3 M to 27.0.
  Alone the rooms wait on the card (`wait` 6.6 ms); after the land the main thread is what holds them.
- **The hand-over's cost follows the rows, not the frame's work**: `place` times `Residency::handOver`, whose plan
  (`Residency::plan`, run where `mPlanStale`) and syncs walk every texture and structure row of the scene, resting ones
  among them: 21 k live slots after the land against 6 k alone.
- The probe let go of a texture a sprite's light bake was made from, which the tour refused twice (`textures-readable`);
  a release has to go through the standing a bake and its source share.

## No more resting than held

`SceneDesc::boundResting` in each hand-over, the oldest freed first, at most 64 rows of a table a hand-over, a file a
live sprite-light bake is made from kept; the same flight whole, against the same rooms:

| flight | frame p99 | first four rooms' p99 | `place` | M cycles a frame | resting vertices | live slots |
|---|---|---|---|---|---|---|
| after the land, bounded | 24.2 ms | 24.9 | 1.14 / 3.9 | 29.9 | 0.85 M | 6197 |

No fault, 440 checks; `repeat --pairs=10` identical on the desk. The land legs' p99 stood at 105.7 ms against 109.1,
their `place` p99 at 4.2 ms against 11.9, at the cost of 49 k meshes arrived against 33 k and 211 MiB uploaded against
147: what the cache no longer kept arrives again.

A first flight without the bake's source kept refused one sprite's light bake ("the texture it bakes is no longer
held"): the bake is shaped from a colour slot of its file, which rests where the sprite names the file in another
wrap, and the table's eviction of its oldest, where it is full, freed it the same way.
