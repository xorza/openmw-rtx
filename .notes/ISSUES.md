# Open issues

- `MWRender::Rtx::Tracer`'s constructor initialises `mExtractor` before `mWidth`, which is not the
  order they are declared in. GCC reports it as `-Wreorder`.

- The `seyda-neen-customs` viewpoint in `files/rtx/views.cfg` puts the camera inside a wall: every
  primary ray hits, and the frame is one texture magnified across the whole image.

- `placeScene` computes the instance records twice for every frame that moves: once in
  `SceneAcceleration::buildTopLevel` and once in `SceneBuffers::place`, each into its own scratch
  buffer. On a nine-by-nine exterior that is 51,742 records built twice.

- A frame that moves costs three submits, each fenced before the next is recorded:
  `SceneAcceleration::refitMeshes`, `SceneAcceleration::buildTopLevel` and the frame itself. The
  device is idle across both boundaries.

- `SceneAcceleration::buildTopLevel` allocates the top-level storage buffer and a build scratch
  buffer through `vkAllocateMemory` on every frame that moves, and frees the scratch again at the
  end of the call.

- `SceneAcceleration::mStructureBytes` is added to on every top-level build and only assigned when
  the bottom levels are made, so it grows without bound across a session.

- `SceneAcceleration::buildTopLevel` asks the driver for a bottom-level structure's device address
  once per instance per frame. On a nine-by-nine exterior that is 51,742 calls a frame against
  handles that last from one `setScene` to the next.

- `SceneBuffers::place` constructs a whole `LightGrid` every frame rather than refilling the one it
  has.

- `MeshInstance` carries its previous transform as `std::optional<osg::Matrixf>`, so every instance
  in the scene pays 72 bytes a frame to record whether it moved.

- `PosedActors` snapshots the world's instances and lights when it is constructed but not its
  sprites or emitters, so the static world's particle systems are gone from the frame after the
  first `unplace`.

- A cell arriving rebuilds the whole scene. `Tracer` answers a changed structure revision with
  `setScene`, which destroys every bottom-level structure, every buffer and the texture array and
  makes them again, and drops the temporal history with them — where what changed is a few hundred
  meshes appended to a table of fifteen hundred.

- In the game the water is the rasterizer's `Water Geometry`, mirrored as an ordinary surface:
  `MaterialKind::Water` is set only by `RtxBridge::addWater`, which only the harness calls. So the
  game's water gets no `MASK_WATER`, no shadow pass-through and none of the wave or caustic
  treatment the renderer has for it.

- `SceneExtractor` keys materials on the address of an `osg::StateSet`. OpenMW gives the water a new
  one every frame as it cycles `textures/water/waterNN.dds`, so the mirror sees a new material each
  frame and sweeps the one it replaced — a table that churns for a surface that has not changed.
