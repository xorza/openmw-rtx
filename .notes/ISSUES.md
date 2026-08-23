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

- `[RTX] upscale` anything but `off` takes the game down on the first traced frame:
  `NGX_VULKAN_EVALUATE_DLSSD_EXT` returns `NVSDK_NGX_Result_FAIL_NotInitialized`. NGX starts, reports
  Ray Reconstruction available and builds the feature without complaint, and the render and output
  extents are the ones the composite path used. `openmw-rtxtool view --upscale=quality` at the same
  extent on the same device is fine, and so was the game before it owned its window, so it is neither
  the size nor DLSS itself. The validation layers say nothing.

- The ray tracing renderer has no screenshot key: `RtxRenderer::saveScreenshot` logs and does
  nothing. `OPENMW_RTX_SHOT` still writes frames.

