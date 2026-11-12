# Open issues

- **A cached navmesh tile ignores an animated collision's pose**: a tile's key in the disk cache holds each source's
  shape and its object's transform (`MeshSource`), and not a compound shape's child transforms, which
  `RecastMeshObject::update` follows. A tile written with an animated part in one pose is read back for every pose of
  that object at that transform.

- **Three presenter tests fail on Windows with no desktop session**: run over SSH on an AMD integrated GPU under
  Windows 11, `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn`, `aFrameUnderFifoWaitsForThePresentTwoBeforeIt`
  and `aSuboptimalPresentRebuildsOnceAnExtent` fail as `vkCreateSwapchainKHR` answers `VK_ERROR_FORMAT_NOT_SUPPORTED`;
  the same binary passes all three in the logged-in desktop session.

- **`openmw-lib.lib` stands near the size a COFF archive holds under MSVC's link-time code generation**: a release
  build on Windows makes it 3.6 GB with each target's PDB, of the 4 GB past which `lib.exe` stops on LNK1248.

- **`env/install_almalinux_deps.sh`'s header names `vcpkg/flags.cmake`** as the file through which the pinned Clang
  builds and links with gcc-toolset-15's libstdc++, start files and binutils; `vcpkg/linux.cmake` names that
  installation.

- **`RtxRayReconstructionFrameTest` runs past a second**: a validated renderer of its own with DLSS, its NGX
  feature made twice, takes 1.0–1.2 s on a high-end NVIDIA laptop GPU.

- **The test suites run past the 30 s and 1 s limits**: the GPU suite's shards take 84–86 s under `./omw test`, and
  about twenty of the fork's tests take 1–4 s each.
