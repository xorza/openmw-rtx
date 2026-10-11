# Open issues

- **A lost window surface ends the game.** `VK_ERROR_SURFACE_LOST_KHR` from an acquire or a present
  goes to `deviceFailed` (`components/rtxvulkan/present/swapchain.cpp`, `checkPresentable`): nothing
  makes a new surface and swapchain for the window, so a display that goes away or a compositor that
  restarts under the game ends it.
