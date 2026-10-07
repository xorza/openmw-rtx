# Review: the fork's diff against upstream

Scope: `git diff 2f0688aa59 HEAD` (merge base with `upstream/master`), without tests and without
`extern/fidelityfx`. Whoever addresses an item deletes it. When a group is empty, delete its heading.

## The frame path allocates, copies, or rebuilds behind a threshold

- [ ] **Blocked: Q1 in `review-upstream-diff_QUESTIONS.md`.** `components/rtx/scene/lightgrid.cpp:83-86,102-106,151-165` — `rebuild` runs each frame
  (`rtxvulkan/scene/scenebuffers.cpp:341`). It matches lamps by index into a list sorted by position
  (`scenedesc.cpp:284`), so a lamp that appears or goes (a glow effect, a bolt) rebuilds the full grid.
  `build` fits the extent exactly to the lamps' reach, so a carried torch or a bolt that moves outward
  fails `covers` and rebuilds the grid on each frame. A lamp that crosses a cell re-`fill`s the full
  `RunList`. Target shape: pad the extent, give lamps stable identities, and update only the bins of the
  lamps that changed. (medium)
- [ ] **Blocked: Q2 in `review-upstream-diff_QUESTIONS.md`.** `components/rtxvulkan/device/memory/slottable.hpp:99-100`, `growablebuffer.cpp:19` — when the rows
  outgrow a copy, `SlotTable::sync` doubles it, makes a new host-written buffer, and rewrites every row.
  The world's top-level row table (`scene/sceneacceleration.hpp:208`) gets this spike on the frame a
  cell pushes it past the threshold. Without resizable BAR, the old and new copies are both in the
  ~246 MiB host-written heap until the graveyard collects the old one. Target shape: fixed-size blocks
  with an address table, as `BlockedBuffer` has, or a capacity set at load. (medium)

## Wrong behaviour in a single place

- [ ] **Blocked: Q3 in `review-upstream-diff_QUESTIONS.md`; the comments are corrected and the lag measured.** `components/rtxvulkan/shaders/lib/historyclamp.glsl:63-66`, `trace/denoise/accumulateclamp.comp:20` —
  both say that the glossy and pane filters keep their means with `heldToFast`. Only
  `accumulateclamp.comp` includes the library. `trace/denoise/specular.comp:126-151` and
  `trace/denoise/pane.comp:77-92` blend with `blendedMean`, with no fast mean and no box. Thus a rough
  reflection, or a pane's light, keeps up to `ACCUMULATE_FRAMES` of old light after a lamp changes on a
  still surface. Commit 2e237e8c8a said that these filters "will" use the clamp. **Decided 2026-10-07: measure
  first.** Target shape: the comments say what the filters do now. Then `./omw release noise --cut=N`
  measures the lag on a lamp change, and the measurement decides whether each filter gets fast means
  and `heldToFast`, as ReLAX clamps specular. (medium)

## One truth has more than one source

- [ ] **Blocked: Q4 in `review-upstream-diff_QUESTIONS.md`.** `components/rtx/renderer/renderer.hpp:438-439,492` — `traceGuiTexture` and `renderFrame` take the
  1616-byte `Shaders::VisibilityConstants` as "the camera". Four parties write its fields: the camera
  builder (`frame/camera.cpp:47-86`), `describeWorld` (`environment/frameworld.cpp:108-214`, which also
  splits the rest into `FrameOptions`), `sampleFrame`, and the backend (`visibilitypass.cpp:484-527`,
  `tracemedia.cpp:58`). `leavesSamplingAlone` (`frame/framesampling.cpp:29-39,46`) exists only to catch a
  writer of another party's field. Target shape: the seam takes a host-side description (eyes, world
  reading, options), and only the backend fills the device block. (medium)

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
