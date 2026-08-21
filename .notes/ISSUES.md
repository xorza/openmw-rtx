# Open issues

- `apps/rtxtool/view.cpp` allocates a full-size float history image the window never reads — 33 MiB
  at 1080p and 133 MiB at 4K — because the composite's descriptor has to point somewhere.
