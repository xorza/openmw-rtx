# Open issues

- **A cached navmesh tile ignores an animated collision's pose**: a tile's key in the disk cache holds each source's
  shape and its object's transform (`MeshSource`), and not a compound shape's child transforms, which
  `RecastMeshObject::update` follows. A tile written with an animated part in one pose is read back for every pose of
  that object at that transform.

- **Three presenter tests fail on Windows with no desktop session**: run over SSH on an AMD integrated GPU under
  Windows 11, `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn`, `aFrameUnderFifoWaitsForThePresentTwoBeforeIt`
  and `aSuboptimalPresentRebuildsOnceAnExtent` fail as `vkCreateSwapchainKHR` answers `VK_ERROR_FORMAT_NOT_SUPPORTED`;
  the same binary passes all three in the logged-in desktop session.
