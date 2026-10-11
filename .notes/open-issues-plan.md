# Open issues: plan

This plan covers the five entries in `.notes/ISSUES.md`. For each entry, the cause was traced in the
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

1. [Item 1](#item-1-the-crash-catcher-says-it-works-when-it-cannot-write-a-report): the game logs a
   crash catcher that cannot write a dump.
2. [Item 2](#item-2-the-card-watch-reads-the-card-the-renderer-runs-on): a bench line can describe
   the wrong card.
3. [Item 3](#item-3-a-lost-surface-gets-a-new-surface): a lost surface ends the game.
4. [Item 4](#item-4-a-window-that-shows-nothing-draws-nothing): a minimized window keeps the card
   busy.
5. [Item 5](#item-5-the-window-size-is-kept-in-the-desktops-unit): the window size drifts.

Items 4 and 5 both change `InputWrapper::handleWindowEvent`. Do item 4 first, so that item 5's diff
starts from it. The other items share no file.

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

## Item 1: the crash catcher says it works when it cannot write a report

**Entry:** "A crash monitor whose report database fails to open is reported as a working catcher on
Linux and macOS." **Upstream:** no. `components/crashcatcher` is the fork's.

**Cause.** `Crash::install` (`components/crashcatcher/crashpadclient.cpp`) waits for
`SharedPage::awaitMonitor`, which asks `Platform::SharedMemory::isOpenedElsewhere`. That answer says
only that the monitor process opened the page. The monitor opens the page in the constructor of
`MonitorState` (`crashpadmonitor.cpp`), before `crashpad::HandlerMain` runs. `HandlerMain` opens
the database with `CrashReportDatabase::Initialize` and returns `EXIT_FAILURE` when it cannot
(`handler/handler_main.cc`). So the game logs "Crash reports go to …" while no handler runs. On
Windows, `isOpenedElsewhere` answers "cannot tell", and the wait passes at once. Windows is safe
today only because `StartHandler` waits for the handler's pipe, which `HandlerMain` serves after the
database opens.

**Fix.**

- The page carries the monitor's state. `Heartbeat` gets a word `mMonitor`: `Starting` (nought, as
  `SharedPage::create` zeroes it), `Watching`, or `Refused`. A second word gives the reason of a
  refusal as an enum: `DatabaseUnopened` or `ReportUnwritable`. The monitor writes the reason
  first, then the state with release order. The game reads the state with acquire order.
- Before `HandlerMain`, the monitor opens the database through Crashpad's own
  `CrashReportDatabase::Initialize(mDatabase)`. `mDatabase` is the path that the game gives
  `StartHandler`, which `MonitorArguments::read` takes from the handler's `--database`. Then the
  monitor calls `PrepareNewCrashReport` and drops the `NewReport` without
  `FinishedWritingCrashReport`, so Crashpad deletes the file. This is the exact path a dump takes
  into the `new` folder. The monitor closes its database object before `HandlerMain` opens its
  own. A monitor that refuses writes `Refused`, ends with a non-zero code, and runs no handler.
- `install` reads the page's state whatever `StartHandler` answers. On Windows, a monitor that
  refuses before `HandlerMain` never writes the pipe, so `StartHandler` fails, and only the page
  holds the reason. `Refused` gives an error that names the reason ("its report folder could not be
  opened", "its report folder takes no new report"). `Starting` after the patience gives "its
  monitor did not start", as now.
- `SharedPage::awaitMonitor` waits for a state that is not `Starting`, on every system, with the
  same ten seconds, and answers the state. `isOpenedElsewhere` then has no production caller:
  remove it, its two system implementations and its test in
  `apps/components_tests/platform/sharedmemory.cpp`.

**Why this fix.** The game's claim must rest on the fact it states: the monitor can write a report.
A probe through Crashpad's own database code finds what `HandlerMain` will find. A probe of the
folder with our own file code can disagree with Crashpad. The state word also closes the Windows
case, where the system cannot tell who opened a mapping, so the three systems take one path.

**Cost.** A disk that fills after the start still loses a dump. No check at the start can see that.
The monitor's summary already logs "left no dump" for such an end. Between the probe and
`HandlerMain`, the folder can change. That window is a few milliseconds and is accepted.

**Tests.**

- Extend `CrashPageTest` (`apps/components_tests/crashcatcher/crashmonitor.cpp`): the monitor's
  side writes `Refused` with each reason, and `awaitMonitor` on the game's side answers that state
  and reason at once. A page left at `Starting` answers `Starting` after a patience of a few
  milliseconds.
- Two modes in the crash matrix (`crashtests.cpp`), one for each reason. `DatabaseUnopened`: the
  report folder exists with mode `0555`, so Crashpad cannot make its subfolders.
  `ReportUnwritable`: the database is made first, then its `new` folder gets mode `0555`. In each
  mode, `install` returns the refusal with that reason, and the mode's log has no "Crash reports go
  to". Windows: the same two modes with a deny ACL, left to the Windows run.

**Verify.** `./omw test components-tests --gtest_filter='CrashPage*:CrashMonitor*'`, then
`./omw test -R crash.matrix --output-on-failure`. Done when both pass and the existing matrix modes
still write their dumps.

## Item 2: the card watch reads the card the renderer runs on

**Entry:** "The harness's card watch reads a card chosen without the renderer's device."
**Upstream:** no.

**Cause.** `Nvml::Nvml` opens device index 0 (`nvmlDeviceGetHandleByIndex_v2(0, …)`).
`AmdGpu::find` takes the first card in `/sys/class/drm` whose vendor is `0x1002`. `CardWatch`
tries AMD only when NVML does not open (`CardWatch::CardWatch`). No part asks which card the
renderer chose in `PhysicalDevice::select`.

**Fix.**

- The core states the device's identity in API-neutral terms: `Rtx::DeviceAddress` in
  `components/rtx/common/`, which holds the PCI vendor and an optional PCI address (domain, bus,
  device, function). `Rtx::Renderer` gets `getDeviceAddress()` beside `describeDevice()`.
- The backend fills it from `VkPhysicalDeviceProperties::vendorID` and from
  `VkPhysicalDevicePCIBusInfoPropertiesEXT`, chained into the `vkGetPhysicalDeviceProperties2` call
  where the physical device lists `VK_EXT_pci_bus_info`. A property query needs the extension
  listed, not enabled, so the device is made as before. It is never a requirement.
- `CardWatch` is made with no card. `watch(const Rtx::DeviceAddress&)` picks the reader by the
  vendor and opens that card only. NVIDIA: `nvmlDeviceGetHandleByPciBusId_v2` with NVML's spelling,
  `"%08x:%02x:%02x.%x"`. amdgpu: `/sys/bus/pci/devices/` with the kernel's spelling,
  `"%04x:%02x:%02x.%x"`. The scan of `/sys/class/drm` goes. One function per spelling, each with a
  test.
- A device with no address, a vendor with no reader on this system, or a reader that does not open
  that card, gives no reading. The report says "card not watched" and the reason. It never guesses a
  card.
- `Session::createRenderer` hands the address to the measurer once the renderer stands, and the
  watch starts there. `Measurer`'s constructor no longer starts it.

**Why this fix.** A PCI address is the one key that the Vulkan driver, NVML and sysfs share for one
card. NVML's device index follows its own order, and Vulkan's order is the loader's. MangoHud and
other overlays match a card by its PCI address for this reason. A refusal is correct where the card
is not known, because a reading of a different card is quietly wrong.

**Cost.** The window before the first place starts when the renderer stands, not when the process
starts. `Engine::go` makes the renderer before `prepareEngine` makes the world and loads the content
files, so the load stays in the window. Only the reading of the configuration falls outside.

**Tests.**

- `AmdGpu` takes its root folder as a parameter, so a test builds a small sysfs tree in a temporary
  folder: two AMD cards at different addresses with different `pp_dpm_sclk` values. The reader at
  address B reads B's clock (hand-written values, exact match), and an unknown address gives no
  reader.
- The two address spellings, with a domain above `0xffff` for NVML's eight digits.
- A test on the device harness: `getDeviceAddress()` on this card has an address, and its vendor is
  `VkPhysicalDeviceProperties::vendorID`. On Linux, a test file that CMake chooses also checks that
  `/sys/bus/pci/devices/<address>/vendor` names the same vendor.

**Verify.** `./omw test components-tests --gtest_filter='*Card*:*AmdGpu*'`,
`./omw test rtx-gpu-tests --gtest_filter='*DeviceAddress*'`, then a short `./omw release bench` leg.
Done when the bench's report has a card line with this card's clock.

## Item 3: a lost surface gets a new surface

**Entry:** "A lost window surface ends the game." **Upstream:** no.

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

## Item 4: a window that shows nothing draws nothing

**Entry:** "A minimized window keeps drawing on KDE Wayland." **Upstream:** yes, under the SDL3
port. **Decision 1** applies.

**Cause.** `InputWrapper::handleWindowEvent` (`components/sdlutil/sdlinputwrapper.cpp`) maps four
event edges to `windowVisibilityChange`: `SHOWN` and `RESTORED` to true, `HIDDEN` and `MINIMIZED` to
false. KWin minimizes a window through the `xdg_toplevel` suspended state. SDL sends
`SDL_EVENT_WINDOW_OCCLUDED` for that state, and `EXPOSED` when it ends
(`SDL_waylandwindow.c`, `XDG_TOPLEVEL_STATE_SUSPENDED`). The wrapper ignores both. macOS sends
`OCCLUDED` from `windowDidChangeOcclusionState`, and X11 sends it with `MINIMIZED` on an unmap.

**Fix.** The code of `sdl3-fixes`:

- `SDLUtil::isWindowShown(SDL_WindowFlags)` in `sdlvideowrapper`: false when `SDL_WINDOW_HIDDEN`,
  `SDL_WINDOW_MINIMIZED` or `SDL_WINDOW_OCCLUDED` is set.
- On each of `SHOWN`, `HIDDEN`, `MINIMIZED`, `RESTORED`, `OCCLUDED` and `EXPOSED`, the wrapper reads
  `SDL_GetWindowFlags` and tells the listener only when the answer differs from what it last told
  it (`mWindowShown`, true at the start, as the listener assumes).
- With Decision 1, nothing else changes: `Engine::frame`, `MenuVideo::run` and the nested loops
  already pause on `isWindowVisible`. The heartbeat stays alive, because `Renderer::awaitFrame`
  counts it before `frame` returns.
- In the same files, the comment on `mWarpMovesPointer` says that Wayland lets no window move the
  pointer. SDL 3.4 can, where the compositor offers `wp_pointer_warp_v1`, and SDL does not say
  whether it does. Correct the comment as `sdl3-fixes` has it. The behaviour stays.

**Why this fix.** SDL keeps the window's state in its flags, and it updates them before it queues
the event (`SDL_SendWindowEvent`). A read of the state cannot disagree with the order of edges that
a compositor chooses. `OCCLUDED` is SDL's portable statement that the window is not on screen, and a
Wayland compositor that suspends a window asks the client to stop drawing.

**Cost.** On Wayland, a window that the compositor suspends during a resize gets an `EXPOSED` and
asks for one frame. The paused game does not draw it, so the compositor keeps the old buffer until
the window shows again. This is the same as a minimized window today.

**Tests.**

- Extend `SDLUtilVideoWrapperTest` with a table of the predicate over the flag combinations, as
  `sdl3-fixes` has it.
- The change filter, under SDL's offscreen driver: a real `InputWrapper` on an offscreen window with
  a recording listener. `SDL_HideWindow` gives one false, a second hide gives nothing, and
  `SDL_ShowWindow` gives one true. SDL's public API cannot make a window occluded, so the predicate
  test alone covers `OCCLUDED`.

**Verify.** `./omw test components-tests --gtest_filter='SDLUtil*'`. Then a check in the real game
with `./omw game`: minimize it through KWin, and read
`nvidia-smi --query-gpu=utilization.gpu --format=csv -l 1`. The title cannot show the frame rate of
frames the game does not draw. Done when the use falls to the desktop's own and comes back on
restore. On macOS and X11, left to a run on that system.

## Item 5: the window size is kept in the desktop's unit

**Entry:** "A windowed game's size drifts on a Wayland desktop with displays at different scales."
**Upstream:** yes, under the SDL3 port. **Decision 2** applies.

**Cause.** The setting holds pixels. `SDLUtil::setVideoMode` turns them into points with the density
of the hidden window (`windowPoints`). SDL gives a hidden Wayland window the largest scale of any
display (`Wayland_CreateWindow`). When the compositor maps the window on a display at a smaller
scale, SDL keeps its points (`Wayland_HandlePreferredScaleChanged`), so it has fewer pixels.
`WindowManager::windowResized` stores those pixels. Each start converts with the wrong density
again.

**Fix.**

- `[Video] window width/height` holds the size in SDL's window unit, which `SDL_GetWindowSize`
  answers: points on macOS and Wayland, pixels on Windows and X11. `openWindow` and
  `setVideoMode` give it to SDL as it is.
- The wrapper reports a size to store on `SDL_EVENT_WINDOW_RESIZED`, and reads it from
  `SDL_GetWindowSize` when it handles the event, not from the event's data: SDL removes superseded
  window events from its queue, and the state is the latest. The layout still reads
  `PIXEL_SIZE_CHANGED`. The listener gets a call for each, as `sdl3-fixes` splits `windowSized`
  from `windowResized`. `WindowManager` stores only in windowed mode, as now.
- The harness needs its window at the frame's size in pixels (`applyHostedSettings`). It states that
  size through `OMW::EngineHost`, and `openWindow` converts it with `windowPoints` and the density
  of the window it just made. A hosted run never stores the size, so its conversion cannot drift.
  `windowPoints` stays for this and for the conversion below.
- A settings file from before this change holds pixels. The settings migration marks such a file,
  and `openWindow` converts its size once, with the pixel density of the desktop mode of the display
  that `[Video] screen` names (`SDL_GetDesktopDisplayMode(display)->pixel_density`). SDL's Wayland
  backend sets that density to the output's scale. On Windows and X11 it is 1, and the size does not
  change.
- `files/settings-default.cfg` and `docs/source/reference/modding/settings/video.rst` say the new
  unit.

**Why this fix.** The drift is a round trip through two densities. With this fix, no density is
read for the player's window, so no round trip exists. The size goes to SDL in SDL's unit and comes
back in it. Desktop toolkits keep a window's geometry in logical units for this reason: Qt's
`saveGeometry` and the browsers' saved windows do. The fork's frame size is `[Video] resolution`, so
the window size carries no promise about pixels. Upstream's SDL2 window size was in SDL2's window
unit too.

**Cost.**

- On Wayland, the compositor and not the game chooses the display a window opens on. Where it
  chooses a display at another scale than the one `[Video] screen` names, the one-time conversion
  gives a window larger or smaller by the ratio of the two scales. After that start the size is
  stable.
- On Windows, the unit is pixels. Windows resizes a window that moves to a display at another DPI,
  and the game stores that size. No conversion runs, so no drift occurs.
- A shown harness window (`./omw view`) on a display at another scale than the largest is not
  pixel for pixel. The presentation scales the frame into it.

**Tests.**

- The harness's conversion and the one-time conversion are one function of a pixel size and a
  density, and `SDLUtilVideoWrapperTest` already covers `windowPoints`. Extend the settings migration's
  test (`apps/components_tests/settings/migration.cpp`): a file with a window size and no mark gets
  the mark, and a file with the mark is left as it is.
- The fault needs two displays at different scales. This machine has one: the internal eDP is
  disabled. The check enables it at scale 1 next to the G9 at 1.5
  (`kscreen-doctor output.eDP-1.enable`), and only with the user's approval, since it changes the
  desktop. Then: start windowed on the eDP, quit, and start again three times. Then the same on the
  G9.

**Verify.** `./omw test components-tests --gtest_filter='SDLUtil*:*Settings*:*Migrat*'`, then the
two-display check. Done when the stored size is the same after each of the starts on each display.
