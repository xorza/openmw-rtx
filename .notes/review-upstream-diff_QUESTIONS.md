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

## Q2. `SlotTable`'s growth on the frame path (`components/rtxvulkan/device/memory/slottable.hpp`)

Five tables grow by doubling: the mesh, instance and material tables (`scenebuffers.hpp:155-175`), the
top-level row table (`sceneacceleration.hpp:208`) and the texel table. A growth makes a new
host-written buffer and rewrites every row on the frame a cell pushes the table past its size. The
item's two target shapes both reach beyond `SlotTable`:

| Option | What it does | Cost |
| --- | --- | --- |
| A. Blocked rows **(recommended for the TLAS row table only)** | The TLAS build takes `arrayOfPointers`, so its rows can live in fixed blocks that never move, as `BlockedBuffer` does. The other four stay flat. | Only the largest table is fixed; the shader-read tables keep their spike. |
| B. Blocked rows everywhere | Every table in fixed blocks with an address table. | Every shader read of an instance, mesh or material row gains an indirection on the hot trace path; a shader-wide change. |
| C. Capacity at load | Each table opens at a budget's size (for example the cell ring's) and never grows. | A budget to choose and enforce; memory held at the budget from the start. |
| D. Keep | Accept a spike a logarithmic number of times per session. | The spike stays. |

Cells arrive in bursts, so growing early or copying over several frames does not help: one cell can
need more rows than the slack. Blocked: the item "`components/rtxvulkan/device/memory/slottable.hpp:99-100`".
