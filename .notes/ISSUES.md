# Open issues

- A freed texture slot keeps the image that was in it until something takes the slot over, so a
  region walked away from goes on costing its texture memory until an equal number arrive.

- A cell arriving rebuilds every bottom-level acceleration structure, not only the ones that
  arrived — 47 ms a crossing on the streaming route. The geometry they were built from lives in one
  device buffer sized to the scene, and appending to it moves it; every structure holds a device
  address into it. `SceneAcceleration` and `SceneBuffers` would have to hold a list of blocks
  instead, with the scene's vertex allocator given the same block size.

- A material carries no texture transform, so a surface whose shading animates by scrolling its UVs
  stands still. `NifOsg::UVController` puts an `osg::TexMat` on the state set and the mirror now
  applies it and reads the state set it lands in, but there is nowhere in `Rtx::Material` for it to
  go. It is the only state-set controller the harness's views reach — 432 of them in Vivec.

- The rasterizer's cull and the mirror both pose every deforming drawable they reach, at traversal
  numbers of their own, so each frame skins twice and the two write different halves of a double
  buffer the previous frame's draw thread is reading.

- `MWRender::Animation` marks a skeleton `Inactive` or `SemiActive` by what the rasterizer thinks is
  worth animating, and `SceneUtil::Skeleton` then refuses to move its bones however it is asked. The
  mirror poses everyone it reaches, but not past that flag.
