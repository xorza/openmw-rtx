# Open issues

- **A Wayland compositor that restarts ends the game.** SDL 3.4.16 closes its display connection and
  sends `SDL_EVENT_QUIT` (`Wayland_HandleDisplayDisconnected`): its reconnect is compiled out
  ("TODO RECONNECT"), so the game quits before the renderer can make a new surface for the window.
