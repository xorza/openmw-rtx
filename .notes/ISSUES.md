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
