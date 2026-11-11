# Open issues

- **`test_script_placed` fails on Windows**: "a script placed object joins the navigation mesh when an
  object is disabled" fails three runs of three on an AMD integrated GPU under Windows 11 (full flavour,
  `7b26b022b8`): the physics meets the placed `hubtemple_pillar01` at x 4272.2, and the navigation ray
  reaches x 4500 through the whole 600-frame wait. The cell grid case of the same suite passes there, and
  both pass on Linux. CI runs the integration tests on Linux alone.

- **A cached navmesh tile ignores an animated collision's pose**: a tile's key in the disk cache holds each source's
  shape and its object's transform (`MeshSource`), and not a compound shape's child transforms, which
  `RecastMeshObject::update` follows. A tile written with an animated part in one pose is read back for every pose of
  that object at that transform.

- **Five GPU tests fail on Windows over SSH**: on an AMD integrated GPU under Windows 11, `omw test` run over SSH
  against the static vcpkg set (`9954fc27dc` and the four-flavour change after it) fails
  `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn`, `aFrameUnderFifoWaitsForThePresentTwoBeforeIt` and
  `aSuboptimalPresentRebuildsOnceAnExtent` (`vkCreateSwapchainKHR` answers `VK_ERROR_FORMAT_NOT_SUPPORTED`),
  `RtxGpuTimerTest.aFrameAccountsForItsOwnDeviceTimePassByPass` (no span after the placement's), and
  `RtxFramesTest.aRequestIsTracedAsItsWorldDescribesItAndCarriesTheAirOn` (the traced constants differ from the
  described ones at 0 s). All pass on Linux with an NVIDIA GPU. Not compared with a build from before the set.

- The Dependency sets workflow's publish job is refused by GitHub on `deps-set-size` ("HTTP 403: Resource not
  accessible by integration" from `gh release create`, run 43030096407), though the job's token has
  `contents: write` and the same job published from `master` (`deps-a41c0e8b02a43c55`) and from
  `pinned-static-build` (`deps-5e394e368d7fabb7`).
