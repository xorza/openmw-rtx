# Open issues

- A cell arriving rebuilds every bottom-level acceleration structure. `extendScene` keeps the
  texture array but still makes `SceneAcceleration` and `SceneBuffers` again when the mesh table
  grows. Appending instead needs the shared position and index buffers to stay where they are, and
  they are sized to the scene and reallocated when it grows — every existing structure holds a
  device address into them.

- Every crossing on a real route compacts, so `extendScene` is never reached: over nineteen
  boundaries flown across Vvardenfell, nineteen were `setScene`. `SceneExtractor::retire` calls
  `SceneDesc::retain` whenever a mesh or material goes, and a departing cell almost always takes one
  with it.

- The texture table is append-only and nothing ever reclaims a slot, so a session that walks through
  enough of the world grows it until it reaches the 4096 the array holds and the renderer throws.

- A state set whose contents change under a stable address is read once and never again.
  `SceneExtractor` keys a material on the `osg::StateSet*`, and `NifOsg::FlipController` animates a
  texture by swapping the attribute inside one — so every animated texture in the game is frozen on
  the frame the mirror first met it.

- With `water shader = true` the world's water has no material at all. `MWRender::Water` clears the
  geometry's own state set and pushes one from a cull callback instead, and the mirror runs before
  cull, so `findOwnStateSet` finds nothing and the surface is placed untagged.
