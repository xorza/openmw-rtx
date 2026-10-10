# Open issues

- **A minimized window keeps drawing on KDE Wayland.** Minimized through KWin with vsync on, the
  game went on tracing and presenting at about 31 frames a second (the title read "31 fps, 32.0 ms"):
  the engine stops drawing on SDL's hidden and minimized events, xdg-shell sends neither, and
  `SDL_EVENT_WINDOW_OCCLUDED` is not handled (`components/sdlutil/sdlinputwrapper.cpp`).

- **Equivalent SPIR-V gives a different frame.** `shadowtiles.comp` with each history tap's unpacked
  word kept in a `vec2[4]` from the gather's `holds`, and read back for the weighted sum, in place of
  loading the tap again: the disassembly differs only in where the unpack sits — the same
  `OpConvertUToF`, the same pinned `OpFMul` by 2^-16 and the same `OpFmaKHR` on the same operands —
  yet the composed frame of ten `seyda-neen-*` views differs on every frame against the form that
  loads twice, the same on every run. Against what `components/rtxvulkan/spirv/spirvpin.hpp` holds:
  that a shader's float arithmetic is the build's and not the driver's.
