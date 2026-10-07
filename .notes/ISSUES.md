# Open issues

- `seyda-neen-ship-armed` (`files/rtx/views.cfg`) reaches a different world under a binary whose
  frames take longer: a 0.5 ms busy wait per placement, and nothing else, moves the scene digest's
  meshes, instances, previous and poses columns on all eight frames of `./omw shot --views=all`.
  Two runs of one binary agree, so the place is a function of the frame time and not of the build.
