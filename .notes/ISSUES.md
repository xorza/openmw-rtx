# Open issues

- `MWRender::Rtx::Tracer`'s constructor initialises `mExtractor` before `mWidth`, which is not the
  order they are declared in. GCC reports it as `-Wreorder`.

- The `seyda-neen-customs` viewpoint in `files/rtx/views.cfg` puts the camera inside a wall: every
  primary ray hits, and the frame is one texture magnified across the whole image.
