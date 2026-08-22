# Open issues

- A cell arriving rebuilds every bottom-level acceleration structure, not only the ones that
  arrived. The geometry they were built from lives in one device buffer sized to the scene, and
  appending to it moves it — every structure holds a device address into it. Appending needs that
  buffer to become blocks that are allocated once and never moved.

- A material freed by `SceneDesc::release` leaks its layer run and the masks behind it until the
  scene is replaced. A run is variable length and reclaiming one needs the suballocator the meshes
  have and the layers do not, so what accumulates is a blend map per terrain chunk walked past.

- The texture table is append-only and nothing ever reclaims a slot, so a session that walks through
  enough of the world grows it until it reaches the 4096 the array holds and the renderer throws.

- A state set whose contents change under a stable address is read once and never again.
  `SceneExtractor` keys a material on the `osg::StateSet*`, and `NifOsg::FlipController` animates a
  texture by swapping the attribute inside one — so every animated texture in the game is frozen on
  the frame the mirror first met it.

- With `water shader = true` the world's water has no material at all. `MWRender::Water` clears the
  geometry's own state set and pushes one from a cull callback instead, and the mirror runs before
  cull, so `findOwnStateSet` finds nothing and the surface is placed untagged.
