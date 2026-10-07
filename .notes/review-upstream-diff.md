# Review: the fork's diff against upstream

Scope: `git diff 2f0688aa59 HEAD` (merge base with `upstream/master`), without tests and without
`extern/fidelityfx`. Whoever addresses an item deletes it. When a group is empty, delete its heading.

## The frame path allocates, copies, or rebuilds behind a threshold

- [ ] `components/rtx/scene/lightgrid.cpp:83-86,102-106,151-165` — `rebuild` runs each frame
  (`rtxvulkan/scene/scenebuffers.cpp:341`), and a lamp whose box changes cells — a carried torch, every
  few frames — re-`fill`s the whole `RunList`; a lamp that appears, goes or moves past the extent
  `build`s it again. At 400 lamps over 3×3 exterior cells (a 51×51×8 grid, 58,364 entries, `-O2`, this
  desk) `build` takes 98 µs and `fill` 95 µs, so padding the extent saves only the 3 µs between them:
  the frames that pay ~0.1 ms are the ones on which a lamp crosses a cell. **Decided 2026-10-08: an
  incremental grid.** Target shape: each cell keeps fixed-capacity slots (or a delta list), a lamp
  that moves writes only the cells it left and entered, and a cell that overflows has a rule of its
  own; `lightRunInCell` (`lib/lights.glsl`) and the upload in `scenebuffers.cpp` read the new layout.
  (medium)
- [ ] `components/rtxvulkan/device/memory/slottable.hpp:99-100`, `growablebuffer.cpp:19` — when the rows
  outgrow a copy, `SlotTable::sync` doubles it, makes a new host-written buffer, and rewrites every row,
  on the frame a cell pushes the table past its size. Five tables grow so: the mesh, instance and
  material tables (`scenebuffers.hpp:155-175`), the top-level row table (`scene/sceneacceleration.hpp:208`)
  and the texel table. Without resizable BAR, the old and new copies are both in the ~246 MiB
  host-written heap until the graveyard collects the old one. Cells arrive in bursts, so growing early
  or copying over several frames does not help: one cell can need more rows than the slack. **Decided
  2026-10-08: the top-level row table alone.** Target shape: its rows in fixed blocks that never move,
  which the build reaches by `arrayOfPointers`, as `BlockedBuffer` keeps its rows; the four tables the
  shaders read stay flat, since a block there is an indirection on every read of the trace. (medium)

## One truth has more than one source

- [ ] `components/rtx/renderer/renderer.hpp:438-439,492` — `traceGuiTexture` and `renderFrame` take the
  1616-byte `Shaders::VisibilityConstants` as "the camera". Four parties write its fields: the camera
  builder (`frame/camera.cpp:47-86`), `describeWorld` (`environment/frameworld.cpp:108-214`, called by
  the game's `SkyReader::describe`, which also splits the rest into `FrameOptions`), `sampleFrame`, and
  the backend (`visibilitypass.cpp:484-527`, `tracemedia.cpp:58`). `leavesSamplingAlone`
  (`frame/framesampling.cpp:29-39,46`) exists only to catch a writer of another party's field, and the
  harness reads the block back (`FrameReport::mConstants`, the scene digest). **Decided 2026-10-08: a
  host description at the seam**, the largest change in the plan: about 40 files over the core, the
  backend, both hosts (`RtxRenderer::describeTrace` and `trace`, `SkyReader`), `OffscreenTrace`, the
  harness's instruments and their tests. Target shape: `renderFrame` takes a frame request — the two
  eyes as `Shaders::Camera`, the ray mask and the lamp flag, the `WorldReading`, the `FrameOptions` —
  and `traceGuiTexture` the same eyes; the backend calls `describeWorld` and `sampleFrame` and owns the
  block and the fog drift `SkyReader` keeps now; `leavesSamplingAlone` goes; and the harness reads the
  block from the frame result, not from what it handed in. (high)

## Shader structure

- [ ] `components/rtxvulkan/shaders/lib/sprites.glsl:467-520` — `PuffLayers::mLayers[5]`/`mAt[5]`, walked
  by `addPuff`'s insertion loop, are Function-storage arrays in `visibility.rgen.spv` and
  `spritecomposite.rgen.spv`, live across the full sprite walk. Target shape: as above. (low)
- [ ] `components/rtxvulkan/shaders/trace/visibility.rgen:261-281,337-355` — the arms' peel and the
  world's peel are the same body. They differ only in eye, mask, miss record, arms flag and the arms'
  early exit on a miss. Target shape: one `peelLayers(...)` that returns the last `Answer` and whether it
  ended in a miss, called two times. (low)

## Dead code

- [ ] `components/rtxvulkan/shaders/lib/traversal.glsl:886-905` — `solidBetween` has no caller. Its doc
  speaks of the removed bounce reuse. Target shape: delete it. (low)
- [ ] `components/rtxvulkan/shaders/lib/fog.glsl:186-190` — `fogExtinctionAt` has no caller. Target shape:
  delete it, and point the `fogColumnOver` doc (`:560`) to `fogDensityAt`. (low)
- [ ] `components/rtxvulkan/shaders/lib/surfacematch.glsl:124-133` — `samePlane` is split out for the
  removed bounce reuse. Its only caller is `heldSurfaceMatches` (`:185`). Target shape: put it into the
  caller and remove the reuse words. (low)
- [ ] `components/rtxvulkan/trace/spritebin.hpp:115`, `spritebin.cpp:130` — `SpriteBin::getBytes` has no
  caller, and the bins' tables are not in `SceneStats::mTableBytes` (`scene/devicescene.cpp:197`). Target
  shape: count the bins in the report, or delete the accessor. (low)

## Stale or false comments

- [ ] `components/rtx/renderer/frameimage.cpp:68` — says that the rasterizer's thumbnail is cut by
  `Misc::cropToAspect`. `MWRender::ScreenshotManager` crops in double and does not call it. Target shape:
  the comment says what the rasterizer does. (low)
- [ ] `components/rtxvulkan/shaders/lib/traversal.glsl:366` — the `MEET_BY_CHANCE` doc lists "the reuse's
  rays". (low)
- [ ] `components/rtxvulkan/shaders/lib/payload.glsl:10` — says "twenty-five words". The struct has
  twenty-six (`:144`). (low)
- [ ] `components/rtxvulkan/shaders/lib/bindings.glsl:350-352` — gives cell `c`'s lamps as
  `at[at[c]] .. at[at[c + 1]]`, but `lightRunInCell` (`lib/lights.glsl:95-98`) indexes `2c + key`, with two
  runs for each cell. (low)
- [ ] `components/rtxvulkan/shaders/lib/bindings.glsl:308` — says `IndexList` is "the light grid's, and
  the sprite tiles'". The sprite tiles read `SpriteTileList` (`lib/spritelist.glsl:65`). (low)
- [ ] `components/rtxvulkan/trace/tracemedia.hpp:59` — "Nothing may be in flight: `WavePass::describe`
  says why" contradicts `WavePass::describe` (`wavepass.hpp:35-37`) and `VulkanRenderer::setSea`
  (`vulkanrenderer.hpp:115-117`). Target shape: one statement of the contract. (low)
- [ ] `components/rtxvulkan/trace/tracechain.hpp:39-41` — says pictures "are traced and waited for one at
  a time". Pictures are deferred, and nothing waits for them (`picturetracer.hpp:40-44`). One bin is safe
  only because of queue order (`spritebin.hpp:82-83`). Target shape: give that reason here. (low)
- [ ] `components/myguirtx/rendermanager.cpp:96,199,268` — `checkTexture` says that the backend supports
  external textures, but `doRender` `static_cast`s each `ITexture` to `SlotTexture`, which is undefined for
  an external one. `:268` says "two pipelines", but `GuiPass` has five (`gui/guipass.hpp:225-229`). Target
  shape: correct comments, and an assert that the texture is a `SlotTexture`. (low)
- [ ] `components/rtx/view/offscreentrace.hpp:80` — names `readGuiTexture`, which no longer exists
  (`Renderer::takeGuiCopy`/`takeCopy`). (low)
- [ ] `components/rtx/environment/moonbuilder.cpp:72-74` — `foldedPhase` says "Morrowind's phases are
  multiples of a quarter pi, so the fold is a subtraction". The phase is now the continuous
  `MoonState::mPhaseEighths`. (low)
- [ ] `components/sky/vertexrules.hpp:41-46` — says "exactly white, and nothing else", but
  `starVertexShown` tests only `colour.x() == 1.f`. Target shape: the doc states the red-channel rule. (low)

## Include and header conventions

- [ ] `components/misc/presentation.hpp:1`, `components/sdlutil/sdldisplay.hpp:1` — new fork headers with
  `#ifndef` guards. Target shape: `#pragma once`. (low)
- [ ] `components/sceneutil/stableidentity.hpp:66` — uses `typeid` without `<typeinfo>`. (low)
- [ ] `components/crashcatcher/crashimagelinux.cpp:84,92` — uses `std::size_t` without `<cstddef>`. (low)
- [ ] `components/rtxvulkan/pipeline/shadercode.cpp:97` — uses `std::move` without `<utility>`. (low)
