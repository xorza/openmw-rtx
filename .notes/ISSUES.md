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
- A thin sun shadow loses its depth in the denoised frame. A bar 40 units wide, 100 units over a
  floor under an overhead sun, casts a shadow 7 to 9 pixels wide at 96 pixels square; in a still
  frame after 96 frames, the denoised umbra stands at 32 to 48 of 255 where the raw frame's stands
  at 17 to 22, and under the upscaler at `quality` at 52 to 66.
- `RtxBatchTest.oneBlockTakesUploadAfterUploadAndAnotherOnlyWhereOneWillNotFit` fails in some
  shuffled orders (`rtx.gpu.0` at seeds 11627 and 76126). Where an earlier test left a spare
  staging block larger than `sStagingBlock` plus the first two uploads, the first upload takes that
  block, the upload past `sStagingBlock` lands in it at offset 176, and the test expects a block of
  its own at offset nought. The tests that left one at those seeds stage 9 MiB and 36 MiB:
  `RtxGuiDrawTest.lendsOfOneFrameSitEndToEndAndALendPastABlockTakesItsOwn` and
  `RtxTextureArrayTest.aTextureTheDeviceHasNoRoomForComesDownALevelOrDrawsTheStandIn`.
