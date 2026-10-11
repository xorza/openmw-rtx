# Open issues: plan

This plan covers the five entries in `.notes/ISSUES.md`. For each entry, the cause was traced in the
code and, where the fault is in SDL or Crashpad, in their pinned sources (SDL 3.4.16 under
`build-container/package/fetched/sdl3`, Crashpad under `build-package/extern/fetched/crashpad`). Each
item gives a structural fix: the fix removes the cause, and does not add a guard where the fault
shows.

## How to read an item

- **Cause** is what the trace found. **Fix** is the change. **Why this fix** says what the fix
  promises, and which established practice it follows.
- **Upstream** marks an item that changes an upstream file. AGENTS.md lets a bug fix in upstream
  code go in only after the user approves it. [Decision 3](#decisions) records the approval. An
  approved item adds its line to AGENTS.md's Accepted diff in the same commit as the code.
- **Tests** are what the item adds. **Verify** is the run before the commit: build the touched
  targets, then `./omw test <binary> --gtest_filter=...`. Run `./omw gate` once at the end of the
  plan.
- When an item is done, delete its entry from `.notes/ISSUES.md` in the same commit. When a part of
  an entry stays open, write the remaining part as a new entry.

## Order

The items are independent. Do them in this order, which puts a quietly wrong answer first:

1. [Item 1](#item-1-the-crash-catcher-says-it-works-when-its-database-refuses-writes): the game logs a
   crash catcher that cannot write a dump.
2. [Item 2](#item-2-the-card-watch-reads-the-card-the-renderer-runs-on): a bench line can describe
   the wrong card.
3. [Item 3](#item-3-a-lost-surface-gets-a-new-surface): a lost surface ends the game.
4. [Item 4](#item-4-a-window-that-shows-nothing-draws-nothing): a minimized window keeps the card
   busy.
5. [Item 5](#item-5-the-window-size-is-kept-in-the-desktops-unit): the window size drifts.

## Decisions

The user took these calls on 2026-10-11. Each item that a decision changes says so.

1. **What occlusion does (item 4). Taken on 2026-10-11: pause.** A window that shows nothing pauses
   the game, as a minimized window does now. The engine has one answer for a window that the player
   cannot see.
2. **The unit of `[Video] window width/height` (item 5). Taken on 2026-10-11: the desktop's window
   unit.** The value goes to SDL in SDL's own unit, so no conversion runs and the drift cannot
   occur. The harness states its pixel size through `OMW::EngineHost`. Cost: a settings file from
   the fork before this change holds pixels, and on a scaled Wayland or macOS desktop its first
   start opens a window larger by the scale. The other choice kept pixels and set the size again
   after the window mapped, and it depended on when each compositor sends its events.
3. **Approval of the upstream changes (items 4 and 5). Taken on 2026-10-11: both approved.** Both
   fix a fault of the fork's SDL3 port in upstream files. Each adds a sub-line under the SDL3 port
   in AGENTS.md's Accepted diff, in the same commit as its code.

## Item 1: the crash catcher says it works when its database refuses writes

**Entry:** "A crash monitor whose report database fails to open is reported as a working catcher on
Linux and macOS."

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
  refusal as an enum: the database did not open, or a new report could not be written.
- The monitor opens the database before `HandlerMain`, through Crashpad's own
  `CrashReportDatabase::Initialize(mDatabase)`. Then it calls `PrepareNewCrashReport` and drops the
  `NewReport` without `FinishedWritingCrashReport`, so Crashpad deletes the file. This is the exact
  path a dump takes into the `new` folder. The monitor then stores `Watching` or `Refused` with
  release order. A monitor that refuses ends with a non-zero code, and runs no handler.
- `SharedPage::awaitMonitor` waits for a state that is not `Starting`, on every system, with the
  same ten seconds. It answers the state. `install` returns an error for `Refused` that names the
  reason ("its report folder takes no new report"). `isOpenedElsewhere` then has no production
  caller: remove it and its test in `apps/components_tests/platform/sharedmemory.cpp`.

**Why this fix.** The game's claim must rest on the fact it states: the monitor can write a report.
A probe through Crashpad's own database code finds what `HandlerMain` will find. A probe of the
folder with our own file code can disagree with Crashpad. The state word also closes the Windows
case, where the system cannot tell who opened a mapping, so the three systems take one path.

**Cost.** A disk that fills after the start still loses a dump. No check at the start can see that.
The monitor's summary already logs "left no dump" for such an end. Between the probe and
`HandlerMain`, the folder can change. That window is a few milliseconds and is accepted.

**Tests.**

- `crashmonitor.cpp`: a page that the test writes `Refused` into makes `awaitMonitor` answer
  `Refused` at once. A page left at `Starting` answers after the patience, with a short patience.
- The crash matrix (`crashtests.cpp`): a mode whose report folder takes no writes
  (`chmod 0555` on POSIX, a deny ACL on Windows). `install` must return the refusal, and the game
  log must not have "Crash reports go to".

**Verify.** `./omw test components-tests --gtest_filter='*Crash*'`, then
`./omw test -R crash.matrix --output-on-failure`.

## Item 2: the card watch reads the card the renderer runs on

**Entry:** "The harness's card watch reads a card chosen without the renderer's device."

**Cause.** `Nvml::Nvml` opens device index 0 (`nvmlDeviceGetHandleByIndex_v2(0, …)`).
`AmdGpu::find` takes the first card in `/sys/class/drm` whose vendor is `0x1002`. `CardWatch`
tries AMD only when NVML does not open (`CardWatch::CardWatch`). No part asks which card the
renderer chose in `PhysicalDevice::select`.

**Fix.**

- The core states the device's identity in API-neutral terms: `Rtx::DeviceAddress` in
  `components/rtx/common/`, which holds the PCI vendor and an optional PCI address (domain, bus,
  device, function). `Rtx::Renderer` gets `getDeviceAddress()` beside `describeDevice()`.
- The backend fills it from `VkPhysicalDeviceProperties::vendorID` and
  `VK_EXT_pci_bus_info`. The extension is asked where the device offers it, and is never a
  requirement. NVIDIA's and AMD's drivers on Windows and Linux, and RADV, offer it.
- `CardWatch` gets the address. It picks its reader by the vendor and opens that card only:
  `nvmlDeviceGetHandleByPciBusId_v2` for NVIDIA, `/sys/bus/pci/devices/<address>/` for amdgpu. The
  scan of `/sys/class/drm` goes.
- A device with no address, or a reader that does not open that card, gives no reading. The report
  says "card not watched" and the reason. It never guesses a card.
- `Session::createRenderer` hands the address to the measurer once the renderer stands. The watch
  starts there and not in `Measurer`'s constructor.

**Why this fix.** A PCI address is the one key that the Vulkan driver, NVML and sysfs share for one
card. NVML's device index follows its own order, and Vulkan's order is the loader's. MangoHud and
other overlays match a card by its PCI address for this reason. A refusal is correct where the card
is not known, because a reading of a different card is quietly wrong.

**Cost.** The window before the first place starts when the renderer stands, not when the process
starts. The engine makes the renderer before it loads a cell, so the load stays in the window. The
time to read the content files before that is no longer watched.

**Tests.**

- `AmdGpu` takes a root folder, so a test builds a small sysfs tree in a temporary folder: two AMD
  cards at different addresses with different `pp_dpm_sclk` values. The watch at address B reads
  B's clock (hand-written values, exact match), and an unknown address gives no reader.
- A test on the device harness: `getDeviceAddress()` on this card names vendor `0x10de` and the bus
  address that `/sys/bus/pci/devices` lists for it.

**Verify.** `./omw test components-tests --gtest_filter='*Card*:*AmdGpu*'`,
`./omw test rtx-gpu-tests --gtest_filter='*DeviceAddress*'`, then a short `./omw release bench` leg whose report has the card line.

## Item 3: a lost surface gets a new surface

**Entry:** "A lost window surface ends the game."

**Cause.** `Swapchain::checkPresentable` (`components/rtxvulkan/present/swapchain.cpp`) sends
`VK_ERROR_SURFACE_LOST_KHR` to `deviceFailed`. `Surface` makes its handle once, in its constructor,
and nothing makes it again.

**Fix.**

- `Swapchain::acquire` answers `Acquired::Lost`. `Swapchain::present` answers an enum `Presented`
  (`Shown`, `Stale`, `Lost`) in place of its `bool`.
- `Presenter` keeps `mLost`. `wantsResize` answers yes for a lost surface before it asks the surface
  anything: `surfaceIsHidden` asks the capabilities, and a lost surface answers that call with the
  same error.
- `Presenter::rebuild` on a lost surface does this, in this order: wait for the device, release the
  sync objects, destroy the swapchain, call `Surface::remake()`, and make the swapchain again. A
  surface must outlive the swapchains made on it, so the swapchain goes first, and `oldSwapchain`
  stays null because it cannot cross surfaces.
- `Surface` keeps its `SDL_Window*`. `remake()` destroys the handle and asks
  `SDL_Vulkan_CreateSurface` again. The handle changes in place, so the references that `Swapchain`
  and `Presenter` hold stay valid.
- `Swapchain` chooses its format and its present mode again for the new surface: a new surface can
  offer a different list. `setVerticalSync` on a lost surface only stores the setting, and the
  rebuild reads it.
- A new surface that SDL does not make, or that the device's queue does not present to, ends the
  game through `deviceFailed` with that reason. Then no window exists to draw into.

**Why this fix.** The Vulkan specification makes a lost surface the surface's state, not the
device's. DXVK's presenter answers it in this way: `recreateSwapChain` destroys the surface on
`VK_ERROR_SURFACE_LOST_KHR`, makes a new one from the same window, and makes the swapchain again.
The game's window stays, so the surface is the only thing to make again.

**Scope.** On Wayland, a compositor that restarts closes SDL's display connection. SDL 3.4.16 then
sends `SDL_EVENT_QUIT` (`Wayland_HandleDisplayDisconnected`), because its reconnect is compiled out
("TODO RECONNECT"). This item cannot keep the game in that case. When this item is done, replace
the entry with that remaining fault.

**Tests.** A GPU test under SDL's offscreen driver, which makes a surface through
`VK_EXT_headless_surface` (this card's driver offers it). The test presents frames, then puts the
presenter into the lost state through the path the error takes, and presents again. It checks that
the surface handle changed and that each later present answers `Shown`.

**Verify.** `./omw test rtx-gpu-tests --gtest_filter='*Present*'`, then `./omw test`.

## Item 4: a window that shows nothing draws nothing

**Entry:** "A minimized window keeps drawing on KDE Wayland." **Upstream:** yes, under the SDL3
port. **Decision 1** applies.

**Cause.** `InputWrapper::windowEvent` (`components/sdlutil/sdlinputwrapper.cpp`) maps four event
edges to `windowVisibilityChange`: `SHOWN` and `RESTORED` to true, `HIDDEN` and `MINIMIZED` to
false. KWin minimizes a window through the `xdg_toplevel` suspended state. SDL sends
`SDL_EVENT_WINDOW_OCCLUDED` for that state, and `EXPOSED` when it ends
(`SDL_waylandwindow.c`, `XDG_TOPLEVEL_STATE_SUSPENDED`). The wrapper ignores both. macOS sends
`OCCLUDED` from `windowDidChangeOcclusionState`, and X11 sends it with `MINIMIZED` on an unmap.

**Fix.**

- The wrapper derives the state and does not map edges. On each of `SHOWN`, `HIDDEN`, `MINIMIZED`,
  `RESTORED`, `OCCLUDED` and `EXPOSED`, it reads `SDL_GetWindowFlags`. The window shows nothing when
  `SDL_WINDOW_HIDDEN`, `SDL_WINDOW_MINIMIZED` or `SDL_WINDOW_OCCLUDED` is set. The wrapper tells the
  listener only when that answer changes.
- The predicate is a free function in `SDLUtil` on `SDL_WindowFlags`, so a test reaches it with no
  window.
- With Decision 1, nothing else changes: `Engine::frame`, `MenuVideo::run` and the
  nested loops already pause on `isWindowVisible`. The heartbeat stays alive, because
  `Renderer::awaitFrame` counts it before `frame` returns.

**Why this fix.** SDL keeps the window's state in its flags, and it updates them before it sends the
event (`SDL_SendWindowEvent`). One read of the state cannot disagree with an order of edges that a
compositor chooses. `OCCLUDED` is SDL's portable statement that the window is not on screen, and a
Wayland compositor that suspends a window asks the client to stop drawing.

**Cost.** On Wayland, a window that the compositor suspends during a resize gets an `EXPOSED` and
asks for one frame. The paused game does not draw it, so the compositor keeps the old buffer until
the window shows again. This is the same as a minimized window today.

**Tests.** A table test of the predicate over the flag combinations, and a test of the wrapper's
change filter with a recording listener: the event list of the entry
(`FOCUS_LOST`, `OCCLUDED`, `EXPOSED`) gives false, then true, and nothing for a repeat.

**Verify.** `./omw test components-tests --gtest_filter='*SdlUtil*'`. Then the user minimizes the
game on KWin once, with `./omw view`, and reads the title's frame rate.

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
  `setVideoMode` give it to SDL as it is, and `SDLUtil::windowPoints` goes.
- The store reads `SDL_EVENT_WINDOW_RESIZED`, which carries the window unit. The layout still reads
  `PIXEL_SIZE_CHANGED`. Today one handler does both, so the listener gets a call for each.
- The harness needs its window at the frame's size in pixels (`applyHostedSettings`). It states
  that size through `OMW::EngineHost`, and `openWindow` converts it with the density of the window
  it just made. A hosted run never stores the size, so its conversion cannot drift.
- `files/settings-default.cfg` and `docs/source/reference/modding/settings/video.rst` say the new
  unit.

**Why this fix.** The drift is a round trip through two densities. With this fix, no density is
read, so no round trip exists. The size goes to SDL in SDL's unit and comes back in it. Desktop
toolkits keep a window's geometry in logical units for this reason: Qt's `saveGeometry` and the
browsers' saved windows do. The fork's frame size is `[Video] resolution`, so the window size
carries no promise about pixels. Upstream's SDL2 window size was in SDL2's window unit too.

**Cost.** See Decision 2. On this machine (one display at 1.5), the stored 2560-pixel width opens
once as 2560 points, 3840 pixels, and the compositor limits it to the display.

**Tests.** The fault needs two displays at different scales. This machine has one: the internal
eDP is disabled. The test enables it at scale 1 next to the G9 at 1.5
(`kscreen-doctor output.eDP-1.enable`), and only with the user's approval, since it changes the
desktop. Then: start windowed on the eDP, quit, and start again three times. The stored size must
not change. A unit test covers the harness's conversion of a stated pixel size.

**Verify.** `./omw test components-tests --gtest_filter='*SdlUtil*:*Settings*'`, then the two-display
check above.
