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

## Q3. The glossy and pane filters' lag (`trace/denoise/specular.comp`, `trace/denoise/pane.comp`)

The comments now say what the code does: only the bounce is clamped. Measured on this card, with a
grey sky that halves after 64 still frames (`--denoise`, the clamp on, `-O2`, a temporary test):

| Light | Frame 1 after | Frame 16 | Frame 32 | Frame 63 | Within 10% of the new level |
| --- | --- | --- | --- | --- | --- |
| Glossy metal floor, roughness 64/255 (target 0.151) | 0.250 | 0.207 | 0.184 | 0.163 | frame 56 |
| Glossy metal floor, roughness 128/255 (target 0.146) | 0.240 | 0.198 | 0.177 | 0.157 | frame 56 |
| Glossy metal floor, roughness 200/255 (target 0.133) | 0.214 | 0.177 | 0.159 | 0.142 | frame 54 |
| Half-opaque pane before the sky (target 0.188) | 0.224 | 0.210 | 0.201 | 0.192 | between frames 16 and 32 |
| The bounce, clamped (`RtxBounceClampTest`) | | | | | frame 31 |

The glossy reflection takes 54–56 frames to come within a tenth, against the clamped bounce's 31.

| Option | What it does | Cost |
| --- | --- | --- |
| A. Clamp both **(recommended)** | `specular.comp` and `pane.comp` keep fast means and clamp through `heldToFast`, as ReLAX clamps specular. | Two more history images a filter, a 5×5 square of loads each, and a test like `theFloorFollowsASkyWhoseLightHalves` for each. |
| B. Clamp the glossy filter only | The pane lags less than the reflection; clamp only the reflection. | The pane keeps its lag. |
| C. Keep | Accept the lag. | A reflection keeps old light for about a second at 60 frames a second. |

Blocked: the item "`components/rtxvulkan/shaders/lib/historyclamp.glsl:63-66`".

## Q4. What the seam's frame calls take (`components/rtx/renderer/renderer.hpp:438-439,492`)

`renderFrame` and `traceGuiTexture` take the 1616-byte device block `Shaders::VisibilityConstants`.
Four parties write into it: the camera builder (`frame/camera.cpp`), `describeWorld`
(`environment/frameworld.cpp`, called by the game's `SkyReader::describe`), `sampleFrame` and the
backend. The harness also reads the block back (`FrameReport::mConstants`, the scene digest). The
item's target moves the block behind the seam. That change reaches about 40 files in the core, the
backend, the game side (`RtxRenderer::describeTrace` and `trace`, `SkyReader`), the view pictures
(`OffscreenTrace`), the harness instruments and their tests. It also moves state: the fog drift that
`SkyReader` keeps for `describeWorld` goes to the backend.

| Option | What it does | Cost |
| --- | --- | --- |
| A. Host description at the seam **(recommended)** | `renderFrame` takes a `FrameRequest` (the two eyes as `Shaders::Camera`, the ray mask and lamp flag, the `WorldReading`, the `FrameOptions`). `traceGuiTexture` takes the same eyes. The backend calls `describeWorld` and `sampleFrame` and owns the block and the fog drift. `leavesSamplingAlone` goes. The harness reads the block from the frame result, not from what it handed in. | The largest change in the plan: the seam, both hosts and about 40 files. |
| B. One sub-block per writer | Keep the block at the seam, but split it into nested structs, one for each writer (camera, world, sampling, backend). Each function takes only its part, so a cross-write does not compile. | A layout change that every shader that reads the block sees. The device type stays in the seam. |
| C. Keep | Delete the item. | The seam keeps a device type, and `leavesSamplingAlone` stays the only guard. |

Blocked: the item "`components/rtx/renderer/renderer.hpp:438-439,492`".
