# Open issues

- **A cached navmesh tile ignores an animated collision's pose**: a tile's key in the disk cache holds each source's
  shape and its object's transform (`MeshSource`), and not a compound shape's child transforms, which
  `RecastMeshObject::update` follows. A tile written with an animated part in one pose is read back for every pose of
  that object at that transform.

- **Three presenter tests fail on Windows with no desktop session**: run over SSH on an AMD integrated GPU under
  Windows 11, `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn`, `aFrameUnderFifoWaitsForThePresentTwoBeforeIt`
  and `aSuboptimalPresentRebuildsOnceAnExtent` fail as `vkCreateSwapchainKHR` answers `VK_ERROR_FORMAT_NOT_SUPPORTED`;
  the same binary passes all three in the logged-in desktop session.

- **`DetourNavigatorAsyncNavMeshUpdaterTest.repeated_tile_updates_should_be_delayed` fails now and then under
  ThreadSanitizer**: on CI's `linux-tsan` (run 43133364352) the second post read `stats.mWaiting.mDelayed` as 0
  where the test expects 49, against a `mMinUpdateInterval` of 250 ms; the run before it on the same tree passed.


- **`openmw-lib.lib` stands near the size a COFF archive holds under MSVC's link-time code generation**: a release
  build on Windows makes it 3.6 GB with each target's PDB, of the 4 GB past which `lib.exe` stops on LNK1248.
