# Open issues

- **A cached navmesh tile ignores an animated collision's pose**: a tile's key in the disk cache holds each source's
  shape and its object's transform (`MeshSource`), and not a compound shape's child transforms, which
  `RecastMeshObject::update` follows. A tile written with an animated part in one pose is read back for every pose of
  that object at that transform.

- **Three presenter tests fail on Windows with no desktop session**: run over SSH on an AMD integrated GPU under
  Windows 11, `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn`, `aFrameUnderFifoWaitsForThePresentTwoBeforeIt`
  and `aSuboptimalPresentRebuildsOnceAnExtent` fail as `vkCreateSwapchainKHR` answers `VK_ERROR_FORMAT_NOT_SUPPORTED`;
  the same binary passes all three in the logged-in desktop session.

- **The frame's thread still computes a few shapes on a warm store**: at `seyda-neen-ship` (`scene`,
  `--upscale=off`, second run) the frame's shapes found 1,390 of 1,391 asked on vanilla, 1,395 of 1,396 on the PBR
  install and 1,242 of 1,249 on the Fullrest Repack, each run writing nothing to the store.
