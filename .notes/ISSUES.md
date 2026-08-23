# Open issues

- A freed texture slot keeps the image that was in it until something takes the slot over, so a
  region walked away from goes on costing its texture memory until an equal number arrive.

- A cell arriving rebuilds every bottom-level acceleration structure, not only the ones that
  arrived — 47 ms a crossing on the streaming route. The geometry they were built from lives in one
  device buffer sized to the scene, and appending to it moves it; every structure holds a device
  address into it. `SceneAcceleration` and `SceneBuffers` would have to hold a list of blocks
  instead, with the scene's vertex allocator given the same block size.

- A surface whose shading animates by scrolling its UVs stands still under the ray tracer.
  `Surface::Material` now carries the scale and offset and `NifOsg::UVController` writes them every
  frame it is applied, but `Rtx::Material` has no field for them and the shaders sample at the
  unmodified coordinate. It is the only state-set controller the harness's views reach — 432 of them
  in Vivec.

- The rasterizer's cull and the mirror both pose every deforming drawable they reach, at traversal
  numbers of their own, so each frame skins twice and the two write different halves of a double
  buffer the previous frame's draw thread is reading.

- `MWRender::Animation` marks a skeleton `Inactive` or `SemiActive` by what the rasterizer thinks is
  worth animating, and `SceneUtil::Skeleton` then refuses to move its bones however it is asked. The
  mirror poses everyone it reaches, but not past that flag.

- With `distant terrain` on, the in-game mirror sees no ground and no paged objects.
  `Terrain::RootNode::accept` forwards to `Terrain::QuadTreeWorld::accept`, which returns
  immediately for any visitor that is not a cull or an intersection visitor, and the chunks it
  would have produced are never children of anything the mirror walks.
