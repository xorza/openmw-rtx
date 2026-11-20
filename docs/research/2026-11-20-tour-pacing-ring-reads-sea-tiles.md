# Three open items of `.notes/todo.md`, measured — 2026-11-20

Whether a tour walks settled, what the cell ring holds that never stands, and what the sea's kernels cost a view by
the sea. Release builds at master `de4d4cccb0`.

## A settled tour against one that streams

`./omw release remote <handheld> tour --replace=config --config=<Repack profile> --expected=mfr --legs=land`, the
sampled land legs, flown settled and then with a switch that streamed as the game does (`RunSetup::mSettled` false),
back to back. Medians are over the 14 legs.

| | settled | streamed |
|---|---|---|
| frame median, ms | 26.8 | 27.2 |
| frame p99, ms | 113.7 | 110.5 |
| worst frame, median of the legs / of all, ms | 223.8 / 641.4 | 214.1 / 642.6 |
| update p99, ms | 84.7 | 85.7 |
| held at crossings, s | 12.36 | 12.33 |
| faults (a pop among them) | 0 | 0 |
| late stands, hitches | 9, 49 | 12, 42 |

The wait a settled walk takes (`CellSupply::awaitOwed`), 111 ms of a 135 ms frame on the desk's Repack tour before the
cell table, is within the flights' spread since the table and `Renderer::standWhole`: what holds the land legs'
p99 is the game's own update loading cells, the same in both. Tours stay settled, and the switch went with the
question.

## What the cell ring holds that never stands

The land legs of the handheld's sampled Repack tour of the same night, summed from each leg's record
(`content.ring`, by distance band from the eye):

| band | 2–3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | all |
|---|---|---|---|---|---|---|---|---|---|
| placements standing | ~50% | 41% | 33% | 29% | 25% | 20% | 14% | 8% | 21% of 939 k |
| meshes standing | ~60% | 54% | 48% | 44% | 39% | 36% | 20% | 12% | 39% of 38 k |

48% of the 11.8 M vertices the ring holds stand, summed over the bands. A leg at a time, the ring's unstood meshes
inside the reach are 0.16–0.66 M vertices, beside 1.1–2.9 M live and 1.8–9.6 M resting: the mesh table's cache of
meshes nothing holds (`MeshTable::rest`), which gives its room back as residency stops standing them. The device held
4791 MiB against a cap of 4842 (medians), that cache filling the room left.

A crossing comes every 43 frames on the land legs, on the desk as on the handheld. Arrival frames are 26% of the
frames, 6.9 meshes each, 1.3 ms over the frame median on the desk and 4.8 ms on the handheld.

The resource caches stood full, 987.5 of 987.7 MiB, having read 12.7% of their NIFs and 10.6% of their nodes again,
against 33% and 29% at `b461faf024` over a whole tour.

## What the sea's kernels cost

`./omw release bench --views=seyda-neen-ship,seyda-neen-shore,vivec-canalworks`, the trace's kernels built with the
sea and without it (a probe that set `VisibilityVariant::mSea` false, the water left out of the picture), three legs a
side in turn after a warm-up, on the desk's NVIDIA laptop card. Medians of the legs' medians.

| place | frame, sea → none | water and media | the sea in the other stages |
|---|---|---|---|
| ship | 6.03 → 5.68 ms | 0.18 ms | 0.09 ms: `direct`, `filter0`, `landing`, `composite` |
| shore | 5.34 → 4.44 ms | 0.43 ms | 0.34 ms, most of it the shadow rays off the water |
| canalworks | 4.70 → 4.72 ms | — | — (no sea in the scene) |

The same on the handheld (`remote <handheld> bench --size=960x540`, the mages guild for a room with no sea):

| place | frame, sea → none | water and media | the sea in the other stages |
|---|---|---|---|
| ship | 47.92 → 44.29 ms | 2.42 ms | 1.1 ms: `direct` 0.43, `landing` 0.34, `bounce` 0.12, `filter0` 0.08 |
| shore | 35.24 → 28.75 ms | 4.22 ms | 2.25 ms: `direct` 0.92, `shadow` 0.36, `bounce` 0.26, `landing` 0.25 |
| mages guild | 28.98 → 28.98 ms | — | — |

The water's and the media's stages shade what sees water, which a tile classed by the sea keeps; what such a tile
class could save is the sea's code in the other stages on the dry tiles alone.
