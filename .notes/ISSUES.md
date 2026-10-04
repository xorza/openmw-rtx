# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02.

- An alpha-blended surface whose material alpha is one and whose texture reaches solid is cut at
  alpha 0.5, where the rasterizer blends its soft texels by the texture's alpha. Every DXT3 leaf,
  banner, rope and sail has a hard edge where the rasterizer's is soft, and a cobweb, a Telvanni
  crystal or Bloodmoon ice whose texture reaches 255 anywhere loses the coverage under the cut.
  `components/rtx/scene/material.hpp` (`Material::isTranslucent`).
- On macOS `openmw-rtxtool` is built into the app bundle's `Contents/MacOS`
  (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`), and the bundle is installed whole, so a macOS install carries
  the harness's executable.
- Upstream's groundcover places a plant in its shapes' own space: `groundcover.vert` turns, scales
  and moves the vertex before the model's transforms apply, where every other reference stands
  above them. A groundcover model whose shapes stand under a transform other than the identity
  draws its plants moved, turned and scaled by it, and the ray tracer, which stands a plant as it
  stands a static, draws them elsewhere. `apps/openmw/mwrender/groundcover.cpp` (`InstancingVisitor`).
- A measured run's host rows move as a whole between runs of one build: at `one-cell-walk` six legs
  held to the performance cores read walk medians of 1.02 to 1.53 ms at a steady clock, and the
  frame thread's cache misses a thousand instructions moved with them, 3.14 to 4.75.
