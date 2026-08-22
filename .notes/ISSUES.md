# Open issues

- A freed texture slot keeps the image that was in it until something takes the slot over, so a
  region walked away from goes on costing its texture memory until an equal number arrive.

- A cell arriving rebuilds every bottom-level acceleration structure, not only the ones that
  arrived. The geometry they were built from lives in one device buffer sized to the scene, and
  appending to it moves it — every structure holds a device address into it. Appending needs that
  buffer to become blocks that are allocated once and never moved.

- A material freed by `SceneDesc::release` leaks its layer run and the masks behind it until the
  scene is replaced. A run is variable length and reclaiming one needs the suballocator the meshes
  have and the layers do not, so what accumulates is a blend map per terrain chunk walked past.

- A state set that only exists during cull is invisible to the mirror, which runs outside it.
  `SceneUtil::StateSetUpdater` as a cull callback pushes its state set onto the cull visitor and
  never touches the node's own, and `NifOsg` attaches it that way for anything marked
  `AnimFlag_AutoPlay`. So a fire or a lava flow is frozen on the frame the mirror first met, and
  `MWRender::Water`'s shader water arrives with no material at all.

- The game's sea is animated off `steady_clock` since the tracer started rather than off the world's
  own clock, so it goes on moving while the game is paused and does not follow time of day.
