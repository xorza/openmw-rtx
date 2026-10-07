# Questions from executing `review-upstream-diff.md`

Each question blocks the plan item that points to it. Answer under the question; the item then
continues.

## Q6. The incremental light grid costs an upload the decision did not price (`components/rtx/scene/lightgrid.cpp`)

Decided on 2026-10-08: an incremental grid, each cell with fixed-capacity slots, a moving lamp
writing only the cells it left and entered. Found while starting it:

- **The list is written to the device whole, every frame** (`scenebuffers.cpp:350-359`): today
  58,364 entries, 233 KB, at 400 lamps over 3×3 exterior cells.
- **Fixed slots make that list cells × 2 keys × capacity long.** The same grid is 20,808 cells, so at
  a capacity of 8 to 32 lamps a cell it is 0.33 to 1.33 million entries, 1.3 to 5.3 MB: a copy of
  that a frame costs more than the 0.1 ms the grid saves on a crossing frame. So the upload must
  become incremental as well: the cells each copy of the table is behind, by frame slot, as
  `SlotTable` tracks its rows.
- **A lamp that comes or goes still renumbers the list**: the lamps are cleared and sorted by
  position every frame (`SceneDesc::orderLights`), so an index is no identity. A moving lamp keeps
  its index, and a lamp that swaps order with another moves both, which an incremental grid handles
  by index. A glow effect or a bolt appearing still builds the grid whole.

| Option | What it does | Cost |
| --- | --- | --- |
| A. Incremental grid and incremental upload **(recommended)** | Fixed slots a cell key, in ascending lamp order (the same list a fresh fill makes); a moved lamp leaves and enters only the cells its box changed; each frame slot's copy writes only the cell keys changed since it was written; overflow builds the grid with a larger capacity. `GpuLightGrid` gains the capacity; `lightRunInCell` reads a count and a stride. | The largest of the light changes: the grid, the upload (dirty cells by slot), the shader's reader and record; 1.3–5.3 MB a copy on the device in a wide exterior, against 233 KB. A lamp that comes or goes still costs a build. |
| B. Fill every frame | Every frame pays the ~0.1 ms fill, so no frame pays more than another. | ~0.1 ms of host time on every frame at 400 lamps; no layout change. |
| C. Keep | Delete the item. | Frames on which a lamp crosses a cell cost ~0.1 ms more. |

Blocked: the item "`components/rtx/scene/lightgrid.cpp:83-86,102-106,151-165`".

## Q7. Blocked top-level rows leave two spikes standing (`components/rtxvulkan/device/memory/slottable.hpp`)

Decided on 2026-10-08: the top-level row table's rows in fixed blocks, which the build reaches by
`arrayOfPointers`. Found while starting it:

- **The pointers are a table that grows too.** `arrayOfPointers` reads one contiguous array of
  addresses, a row each, one copy per frame in flight. Blocks keep the 64-byte rows still, but a growth
  still makes a new pointer array and writes every row's address into it: an eighth of the bytes,
  the same O(rows) on the same frame.
- **The top level grows on that frame as well.** `SceneAcceleration::prepareTopLevel` sizes the
  structure at twice its rows when they outgrow it (`sizeTopLevel`): a new structure, new storage and
  a size query, which the rows' blocks do not touch.
- **A build through pointers reads one more indirection a row on every frame**, and nobody measured
  what that costs this card's top-level build, which runs every frame that moves anything.

| Option | What it does | Cost |
| --- | --- | --- |
| A. Blocked rows, pointers reserved | The rows in fixed blocks; each copy's pointer array opened at a reserve (2^18 rows, 2 MiB a copy) and grown past it only; then a release bench says what the pointers cost the build. | The structure's own growth stays; a build through pointers on every frame. |
| B. Capacity at load **(recommended)** | The row table and the top-level structure made once at a budget's size (for example 2^18 instances) and grown only past it, which no scene the suites hold reaches. | Memory held from the start: 16 MiB of rows a copy in the host-written heap, and the structure's storage at the budget on the device. No growth on the frame path at all. |
| C. Keep | Delete the item. | A growth costs a frame a row copy and a structure, a logarithmic number of times a session. |

Blocked: the item "`components/rtxvulkan/device/memory/slottable.hpp:99-100`".
