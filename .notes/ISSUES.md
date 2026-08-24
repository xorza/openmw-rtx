# Open issues

- A cell arriving rebuilds every bottom-level acceleration structure, not only the ones that
  arrived — 47 ms a crossing on the streaming route. The geometry they were built from lives in one
  device buffer sized to the scene, and appending to it moves it; every structure holds a device
  address into it. `SceneAcceleration` and `SceneBuffers` would have to hold a list of blocks
  instead, with the scene's vertex allocator given the same block size.

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

- `MyGUIPlatform::OSGTexture` allocates an `osg::Image` on every `lock` and an `osg::Texture2D` on
  every `unlock`, and uploads the whole texture either way. A picture written once a frame — the
  video widget — therefore allocates twice a frame and re-sends every pixel of the frame.

- Every `redraw` of an inventory doll or a race preview rebuilds the whole of its scene — the
  bottom-level acceleration structures and the texture array — and a race-creation slider drag
  redraws it on every frame.

- The inventory doll's mirrored scene names one texture with an empty path, which resolves to
  nothing and is drawn with the grey stand-in. The world's scene names none.

- The world map's overlay is composed on the processor: entering a cell box-filters that cell's whole
  local-map tile and uploads the entire overlay texture again, on the frame the cell arrives.

- `Rtx::GuiTextures::add` clears each new texture through a submit it then waits on, so every texture
  the interface creates — one per picture widget, per font atlas, per traced view — costs a queue
  round trip.

- `RtxTool::Chosen` is aggregate-initialised at `apps/rtxtool/main.cpp:297` without `mView` or
  `mNote`, so the build warns twice under `-Wmissing-field-initializers`.

- Reading a scene's vertex normals in `visibility.comp` through a `GL_EXT_buffer_reference` pointer
  gives a different picture from reading the same buffer through a storage-buffer descriptor: with
  the pointer load present but its value discarded the image is byte-identical, and with its value
  used it differs by up to 37 of 255 across a fifth of the pixels.
