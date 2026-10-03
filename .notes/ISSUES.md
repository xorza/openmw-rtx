# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02, and `.notes/REVIEW.md` holds their evidence under the same title.

- An alpha-blended surface whose material alpha is one and whose texture reaches solid is cut at
  alpha 0.5, where the rasterizer blends its soft texels by the texture's alpha. Every DXT3 leaf,
  banner, rope and sail has a hard edge where the rasterizer's is soft, and a cobweb, a Telvanni
  crystal or Bloodmoon ice whose texture reaches 255 anywhere loses the coverage under the cut.
  `components/rtx/scene/material.hpp` (`Material::isTranslucent`).
- On macOS `openmw-rtxtool` is built into the app bundle's `Contents/MacOS`
  (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`), and the bundle is installed whole, so a macOS install carries
  the harness's executable.
- Groundcover is never drawn under the ray tracer; only a log line says so.
  `apps/openmw/mwrender/rtx/rtxrenderer.cpp:214-219`.
- A texture the engine loads in a format the reader does not name — alpha-only `A8`, a sixteen- or
  thirty-two-bit float format, or BC4 — draws as the grey stand-in, and a sky deck in one is left
  out, where the rasterizer samples it. `components/rtx/image/texels.cpp` `readFormat`.
- A measured run's host rows move as a whole between runs of one build: at `one-cell-walk` six legs
  held to the performance cores read walk medians of 1.02 to 1.53 ms at a steady clock, and the
  frame thread's cache misses a thousand instructions moved with them, 3.14 to 4.75.
