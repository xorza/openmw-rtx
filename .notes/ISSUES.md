# Open issues

- **A minimized window keeps drawing on KDE Wayland.** Minimized through KWin with vsync on, the
  game went on tracing and presenting at about 31 frames a second (the title read "31 fps, 32.0 ms"):
  the engine stops drawing on SDL's hidden and minimized events, xdg-shell sends neither, and
  `SDL_EVENT_WINDOW_OCCLUDED` is not handled (`components/sdlutil/sdlinputwrapper.cpp`). An SDL 3.4.18
  window minimized and restored through KWin receives `FOCUS_LOST` and `OCCLUDED`, then `EXPOSED`, and
  no `MINIMIZED`, `HIDDEN`, `SHOWN` or `RESTORED`; it presented at about 20 frames a second while
  occluded.

- **The optimizer reassociates float arithmetic before the pin marks it.** `glslc -O` folded
  `floor((m - 0.5) + 0.5)` in `spriteshade.comp` (`roundHalfAway` of a centre `x = m - 0.5`) to
  `floor(m)` in the module the pin is handed, so the pinned module computes an expression the source
  does not, and the fold decided which multiplies the pin could fuse (`m` gained a second reader).
  `components/rtxvulkan/spirv/spirvpin.hpp` says every add, subtract, multiply and divide is held to
  its order; the optimizer's folds reach the module before that holds.
