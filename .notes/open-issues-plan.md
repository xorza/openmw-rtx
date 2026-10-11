# Open issues: plan

This plan covers one entry in `.notes/ISSUES.md`. The cause was traced in the
code and, where the fault is in SDL or Crashpad, in their pinned sources (SDL 3.4.16 under
`build-container/package/fetched/sdl3`, Crashpad under `build-package/extern/fetched/crashpad`). The
item gives a structural fix: the fix removes the cause, and does not add a guard where the fault
shows.

## How to read an item

- **Cause** is what the trace found. **Fix** is the change. **Why this fix** says what the fix
  promises, and which established practice it follows. **Cost** is what the fix does not cover.
- **Upstream** says whether the item changes an upstream file.
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
