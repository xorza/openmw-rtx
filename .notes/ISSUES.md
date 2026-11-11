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

- **The AppImage's libxkbcommon is older than a host's Compose files**: the image carries AlmaLinux 8's
  `libxkbcommon.so.0`, which reads the host's `/usr/share/X11/locale/*/Compose`; on a current Arch desk every Qt
  program prints `xkbcommon: ERROR: ... unrecognized keysym "dead_hamza"` for each line naming a keysym it predates.

- **The frame's thread misses stored entries every session**: `Store::sResidentBytesMost` (64 MiB) holds less than the
  frame reaches for. The store's entries of 4 KiB or less come to 87 MiB with their nodes (80,314 shapes, 79.6 MiB),
  and the Fullrest Repack's shapes alone to 222 MiB; the start's sweep and the preparation's shape reads
  (`PreparationManifest::standsAsWalked`) fill the bound first come, so a model "stands" with its shape on disk and
  out of memory. At `seyda-neen-ship` on a warm store (`scene`, `--upscale=off`) the frame's walk computed again, all
  with "wrote 0": vanilla 41 shapes (28 ms), the Fullrest Repack 831 shapes (168 ms), the PBR install 50 shapes
  (38 ms); after a switch from another profile the PBR install also missed 594 specular kinds, 103 alpha spans and 19
  texel means (855 ms in all), and the facts lose the room to shapes since the sweep ranks by file time alone. The
  game's warm starts on a new game logged 24 misses (vanilla, PBR) and 140 (Fullrest Repack), each with nothing
  written. `docs/rtx/architecture.md` ("The frame reaches the store's memory alone") states the bound holds what the
  frame needs.

- **A switch between the three installs walks every model they share again**: the manifest holds one entry per model
  path, and a file's stamp is its archive's or folder's path and time (`VfsStamps`), which differ between installs in
  their own folders, so each profile's start overwrites the others' entries. Measured in the game at `7c0383db82`'s
  code: the Fullrest Repack after vanilla walked 6,895 of 45,912 jobs (1.60 s against 0.29 s warm), vanilla after it
  6,895 of 6,913 (0.29 s against 0.04 s), the PBR install after vanilla 6,901 of 6,913; every entry was found in the
  store. `docs/rtx/architecture.md` ("The preparation is incremental by each file's stamp") states a switch between
  profiles walks neither again.
