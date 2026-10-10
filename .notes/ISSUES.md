# Open issues

- **A minimized window keeps drawing on KDE Wayland.** Minimized through KWin with vsync on, the
  game went on tracing and presenting at about 31 frames a second (the title read "31 fps, 32.0 ms"):
  the engine stops drawing on SDL's hidden and minimized events, xdg-shell sends neither, and
  `SDL_EVENT_WINDOW_OCCLUDED` is not handled (`components/sdlutil/sdlinputwrapper.cpp`). An SDL 3.4.18
  window minimized and restored through KWin receives `FOCUS_LOST` and `OCCLUDED`, then `EXPOSED`, and
  no `MINIMIZED`, `HIDDEN`, `SHOWN` or `RESTORED`; it presented at about 20 frames a second while
  occluded.
- **A lost window surface ends the game.** `VK_ERROR_SURFACE_LOST_KHR` from an acquire or a present
  goes to `deviceFailed` (`components/rtxvulkan/present/swapchain.cpp`, `checkPresentable`): nothing
  makes a new surface and swapchain for the window, so a display that goes away or a compositor that
  restarts under the game ends it.
