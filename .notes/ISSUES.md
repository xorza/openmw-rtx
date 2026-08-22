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
