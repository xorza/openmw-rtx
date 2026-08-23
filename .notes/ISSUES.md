# Open issues

- A freed texture slot keeps the image that was in it until something takes the slot over, so a
  region walked away from goes on costing its texture memory until an equal number arrive.

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

- `CI/check_clang_format.sh` fails on the tree at the version it names. Under clang-format 14 seven
  files differ: `apps/components_tests/rtx/requirements.cpp`,
  `apps/components_tests/rtx/visibilitypass.cpp`, `apps/launcher/graphicspage.cpp`,
  `apps/openmw/mwrender/renderingmanager.hpp`, `components/nifosg/controller.cpp`,
  `components/rtxvulkan/compositepass.cpp`, `components/rtxvulkan/exposurepass.cpp`. Under
  clang-format 22 a different set differs, `apps/openmw/mwworld/worldimp.cpp` among them, so the two
  versions cannot both be satisfied.

- `MyGUIPlatform::OSGTexture` allocates an `osg::Image` on every `lock` and an `osg::Texture2D` on
  every `unlock`, and uploads the whole texture either way. A picture written once a frame — the
  video widget — therefore allocates twice a frame and re-sends every pixel of the frame.

- Every `redraw` of an inventory doll or a race preview rebuilds the whole of its scene — the
  bottom-level acceleration structures and the texture array — and a race-creation slider drag
  redraws it on every frame.

- The inventory doll's mirrored scene names one texture with an empty path, which resolves to
  nothing and is drawn with the grey stand-in. The world's scene names none.

- `RtxRenderer::freezeFrame` hands the loading screen one black texel rather than the frame that was
  just presented, so a load on the ray tracing path fades from black instead of from the world.

- The ray tracing renderer has no screenshot key: `RtxRenderer::saveScreenshot` logs and does
  nothing. `OPENMW_RTX_SHOT` still writes frames.

- `MWRender::Rtx::TracedView`'s constructor initialises its members in an order the declarations do
  not match, so every translation unit that includes `tracedview.hpp` and builds the constructor
  warns under `-Wreorder`.

- NPCs in the world are reported animating without their limbs moving. The mirror's own pose is
  live — 210 deforming meshes whose vertex checksum changes every frame — every frame takes the
  `placeScene` path, and that path rewrites the deformed positions into the build buffer, refits
  each bottom-level structure and rewrites the deformed normals. So whatever is frozen is frozen
  after all of that.
