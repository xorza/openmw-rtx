# Open issues: plan

This plan covers one entry in `.notes/ISSUES.md`. For each entry, the cause was traced in the
code and, where the fault is in SDL or Crashpad, in their pinned sources (SDL 3.4.16 under
`build-container/package/fetched/sdl3`, Crashpad under `build-package/extern/fetched/crashpad`). Each
item gives a structural fix: the fix removes the cause, and does not add a guard where the fault
shows.

## How to read an item

- **Cause** is what the trace found. **Fix** is the change. **Why this fix** says what the fix
  promises, and which established practice it follows. **Cost** is what the fix does not cover.
- **Upstream** says whether the item changes an upstream file. AGENTS.md lets a bug fix in upstream
  code go in only after the user approves it. [Decision 3](#decisions) records the approval. An
  approved item adds its line to AGENTS.md's Accepted diff in the same commit as the code.
- **Close first** is a fact the item depends on and the trace did not prove. Close it before the
  fix. When it closes the other way, stop the item and write the result to
  `.notes/open-issues-plan_QUESTIONS.md`.
- **Tests** are what the item adds. Extend an existing test where one covers the same fixture.
- **Verify** is the run before the commit, and the result that counts as done. Build the touched
  targets, then `./omw test <binary> --gtest_filter=...`. Run `./omw test` once and `./omw gate`
  once at the end of the plan. Never run two of these at once: they share the card.
- When an item is done, delete its entry from `.notes/ISSUES.md` in the same commit. When a part of
  an entry stays open, write the remaining part as a new entry.
- This machine runs Linux only. A leg on Windows or macOS is written down in the item and left to a
  run on that system, as `.notes/windows-wer-check.md` does for the WER module.

## Order

Do the items in this order, which puts a quietly wrong answer first:

1. [Item 3](#item-3-a-lost-surface-gets-a-new-surface): a lost surface ends the game.

The items share no file.

## Relation to the upstream MR

The SDL3 port is also upstream's !5670 (`~/Projects/opemw-upstream-prs/sdl3`). The worktree
`~/Projects/opemw-upstream-prs/sdl3-fixes`, branch `sdl3-fixes`, holds the MR's fixes for the same
faults. It is not committed.

- Item 4: the fork takes the same code as `sdl3-fixes` (`SDLUtil::isWindowShown` and the derived
  state in `handleWindowEvent`), so the port stays one change in two places.
- Item 5: the fork and the MR differ on purpose. In the MR, `resolution x/y` is the render size in
  pixels, which Lua's `ui.screenSize` reads, so the MR keeps pixels and sets the window again on a
  change of density. In the fork, the render size is `[Video] resolution`, and the window size is
  free to use the desktop's unit.
- The pointer warp and `std::dec` fixes are already in the fork.

## Decisions

The user took these calls on 2026-10-11. Each item that a decision changes says so.

1. **What occlusion does (item 4): pause.** A window that shows nothing pauses the game, as a
   minimized window does now. The engine has one answer for a window that the player cannot see.
2. **The unit of `[Video] window width/height` (item 5): the desktop's window unit.** The value goes
   to SDL in SDL's own unit, so no conversion runs and the drift cannot occur. The harness states its
   pixel size through `OMW::EngineHost`. The decision accepted one cost: a settings file from before
   this change holds pixels, and its first start opens a window larger by the scale. The review
   added a one-time conversion that removes that cost where the window opens on the display that
   `[Video] screen` names (item 5).
3. **Approval of the upstream changes (items 4 and 5): both approved.** Both fix a fault of the
   fork's SDL3 port in upstream files. Each adds a sub-line under the SDL3 port in AGENTS.md's
   Accepted diff, in the same commit as its code.

## Item 3: a lost surface gets a new surface

**Entry:** "A lost window surface ends the game." **Upstream:** no.

**Blocked:** the test's surface cannot be made without a display. See
`.notes/open-issues-plan_QUESTIONS.md`, item 3. The code is in `.notes/open-issues-plan_item3.patch`.

**Cause.** `Swapchain::checkPresentable` (`components/rtxvulkan/present/swapchain.cpp`) sends
`VK_ERROR_SURFACE_LOST_KHR` to `deviceFailed`. `Surface` makes its handle once, in its constructor,
and nothing makes it again.

**Close first.** The test below needs a swapchain on a `VK_EXT_headless_surface` surface. This
card's driver lists the instance extension. Make sure that a swapchain stands on such a surface
before the test depends on it.

**Fix.**

- `Swapchain::acquire` answers `Acquired::Lost`. `Swapchain::present` answers an enum `Presented`
  (`Shown`, `Stale`, `Lost`) in place of its `bool`.
- `Presenter` keeps `mLost`, the one record of a lost surface. While it is set, `take` answers
  nothing, and nothing asks the surface: `wantsResize` answers yes before `surfaceIsHidden`, which
  asks the capabilities, and `setVerticalSync` only stores the setting, because
  `Swapchain::setVerticalSync` asks the present modes. Both questions fail on a lost surface.
- `RtxWindow::fit` calls `showIn` on each settled frame, so the next frame rebuilds. On a lost
  surface, `Presenter::rebuild` does this, in this order: wait for the device, release the sync
  objects (the present fences settle there), destroy the swapchain, call `Surface::remake()`, and
  make the swapchain again. A surface must outlive the swapchains made on it, so the swapchain goes
  first, and `oldSwapchain` stays null because it cannot cross surfaces.
- `Surface` keeps its `SDL_Window*`. `remake()` destroys the handle and asks
  `SDL_Vulkan_CreateSurface` again. The handle changes in place, so the references that `Swapchain`
  and `Presenter` hold stay valid.
- `Swapchain` chooses its format and its present mode again for the new surface: a new surface can
  offer a different list.
- A new surface that SDL does not make, or that the device's queue does not present to, ends the
  game through `deviceFailed` with that reason. So does a surface made again that is lost before one
  present shows: a rebuild each frame would hold the game in a stall with no word in the log.

**Why this fix.** The Vulkan specification makes a lost surface the surface's state, not the
device's. DXVK's presenter answers it in this way: `recreateSwapChain` destroys the surface on
`VK_ERROR_SURFACE_LOST_KHR`, makes a new one from the same window, and makes the swapchain again.
The game's window stays, so the surface is the only thing to make again.

**Scope.** On Wayland, a compositor that restarts closes SDL's display connection. SDL 3.4.16 then
sends `SDL_EVENT_QUIT` (`Wayland_HandleDisplayDisconnected`), because its reconnect is compiled out
("TODO RECONNECT"). This item cannot keep the game in that case. When this item is done, replace
the entry with that remaining fault.

**Tests.** A GPU test with its own instance, which has the extensions that
`Surface::getInstanceExtensions` names. The device harness makes a headless instance with none. The
test sets `SDL_HINT_VIDEO_DRIVER` to `offscreen` before it starts SDL's video, so the surface comes
from `VK_EXT_headless_surface`. It presents frames, then sets the lost state through the entry that
the `Lost` answers use, and presents again. It checks that the surface handle changed and that each
later present answers `Shown`. A second case loses the new surface again before a present, and
expects the death through `Testing::expectDies` with the reason `deviceFailed` names.

**Verify.** `./omw test rtx-gpu-tests --gtest_filter='*Present*'`. Done when the test passes and
`RtxPresentFenceTest` still passes.
