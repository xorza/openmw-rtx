# Questions from executing `review-upstream-diff.md`

Each question blocks the plan item that points to it. Answer under the question; the item then
continues.

## Q1. The light grid's rebuild (`components/rtx/scene/lightgrid.cpp`)

The item asks to pad the grid's extent and give lamps stable identities, so that a lamp that appears,
goes, or moves outward does not rebuild the grid. A measurement shows this does not remove the cost:
at 400 lamps over 3×3 exterior cells (a 51×51×8 grid, 58,364 entries), `build` takes 98 µs and
`fill` takes 95 µs (`-O2`, this desk). `fill` already runs whenever any lamp's box changes cells,
which a carried torch does every few frames. So the frames that pay ~0.1 ms are the frames on which
some lamp crosses a cell, and padding saves only the 3 µs between `build` and `fill`.

| Option | What it does | Cost |
| --- | --- | --- |
| A. Incremental grid **(recommended)** | Each cell keeps fixed-capacity slots (or a delta list), and a lamp that moves writes only the cells it left and entered. | A new layout that `lightRunInCell` (`lib/lights.glsl`) and the upload in `scenebuffers.cpp` read; a cell that overflows needs a rule. |
| B. Fill every frame | `rebuild` always runs `fill`, so every frame pays the same ~0.1 ms. | ~0.1 ms on every frame instead of on some; no layout change. |
| C. Keep as is | Accept the variance; delete the item. | Frames on which a lamp crosses a cell cost ~0.1 ms more. |

Blocked: the item "`components/rtx/scene/lightgrid.cpp:83-86,102-106,151-165`".
