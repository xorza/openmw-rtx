# Open issues

- **A lost window surface ends the game.** `VK_ERROR_SURFACE_LOST_KHR` from an acquire or a present
  goes to `deviceFailed` (`components/rtxvulkan/present/swapchain.cpp`, `checkPresentable`): nothing
  makes a new surface and swapchain for the window, so a display that goes away or a compositor that
  restarts under the game ends it.
- **A windowed game's size drifts on a Wayland desktop with displays at different scales.** SDL
  gives a new hidden window the largest scale of any display (`Wayland_CreateWindow`), and
  `SDLUtil::setVideoMode` divides the stored pixel size by that density; mapped on a display of a
  smaller scale, the window keeps its points (`Wayland_HandlePreferredScaleChanged`) and opens
  smaller, and `WindowManager::windowResized` stores the smaller size, so each start opens smaller
  again (`apps/openmw/mwrender/renderer.cpp`, `openWindow`).
