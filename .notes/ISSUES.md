# Open issues

- A cell arriving rebuilds every bottom-level acceleration structure. `extendScene` keeps the
  texture array but still makes `SceneAcceleration` and `SceneBuffers` again when the mesh table
  grows, which is 12 ms of spike on a frame that wanted 16.

- Every crossing on a real route compacts, so `extendScene` is never reached: over nineteen
  boundaries flown across Vvardenfell, nineteen were `setScene`. `SceneExtractor::retire` calls
  `SceneDesc::retain` whenever a mesh or material goes, and a departing cell almost always takes one
  with it, so the append path only survives where a whole ring of cells shares its models with the
  ring that stayed.

- The texture table is append-only and nothing ever reclaims a slot, so a session that walks through
  enough of the world grows it until it reaches the 4096 the array holds and the renderer throws.

- In the game the water is the rasterizer's `Water Geometry`, mirrored as an ordinary surface:
  `MaterialKind::Water` is set only by `RtxBridge::addWater`, which only the harness calls. So the
  game's water gets no `MASK_WATER`, no shadow pass-through and none of the wave or caustic
  treatment the renderer has for it.

- `SceneExtractor` keys materials on the address of an `osg::StateSet`. OpenMW gives the water a new
  one every frame as it cycles `textures/water/waterNN.dds`, so the mirror sees a new material each
  frame and sweeps the one it replaced — a table that churns for a surface that has not changed.
