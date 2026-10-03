# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02, and `.notes/REVIEW.md` holds their evidence under the same title.

- An alpha-blended surface whose material alpha is one and whose texture reaches solid is cut at
  alpha 0.5, where the rasterizer blends its soft texels by the texture's alpha. Every DXT3 leaf,
  banner, rope and sail has a hard edge where the rasterizer's is soft, and a cobweb, a Telvanni
  crystal or Bloodmoon ice whose texture reaches 255 anywhere loses the coverage under the cut.
  `components/rtx/scene/material.hpp` (`Material::isTranslucent`).
- Every install ships the harness's `views.cfg`, `benches.cfg` and `rtx/vfs/` scripts, under a
  comment that says no install carries the harness. `CMakeLists.txt:1143-1149`,
  `apps/rtxtool/CMakeLists.txt:91-98`.
- Night-Eye's lift goes into the ambient, which geometry occludes and the exposure meter adapts to, so
  a cave lifted 133 times shows about 3.4 times brighter. `components/rtx/environment/skylight.cpp:210`.
- Groundcover is never drawn under the ray tracer; only a log line says so.
  `apps/openmw/mwrender/rtx/rtxrenderer.cpp:214-219`.
- Textures in BC4, BC6H, BC7 and other formats outside the list draw as the grey
  stand-in, and a sky deck in one is left out. `components/rtx/image/texels.cpp:145-204`.
- The tracer reads `SceneUtil::VertexColorModes::Ambient` as `VertexColour::Tint`, which replaces
  the diffuse colour with the vertex colour. The rasterizer's `getDiffuseColor` keeps the material's
  diffuse under that mode, and only the ambient takes the vertex colour.
  `components/rtx/scene/surface.cpp` `vertexColourOf`.
- A measured run's host rows move as a whole between runs of one build: at `one-cell-walk` six legs
  held to the performance cores read walk medians of 1.02 to 1.53 ms at a steady clock, and the
  frame thread's cache misses a thousand instructions moved with them, 3.14 to 4.75. The per-frame
  walk reads the OSG graph, whose heap layout differs from one process to the next.
- Past the loaded cells, a rendering ray under the ray tracer meets the ground the ring stands and none
  of the ring's statics. The rasterizer meets its paged statics there, with their reference numbers.
- A content file's own clockwise `NiStencilProperty` shows, under the ray tracer, the face the rasterizer
  culls. The material reader cannot tell it from the clockwise front `SceneUtil::attach` states over a
  mirrored body part, whose mirror lives in the skinning and not in the placement.
