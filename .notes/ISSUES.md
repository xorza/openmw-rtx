# Open issues

- **`--pipeline-code` ends a run on AMD's Windows driver**: on a low-end AMD integrated GPU under AMD's Windows driver
  26.8.1, `openmw-rtxtool shot --pipeline-code=<dir>` stops with `vkGetPipelineExecutableInternalRepresentationsKHR
  failed: VK_INCOMPLETE (5), while compiling the pipeline "sprite emitters"` (`Device::reportPipeline`).
- **The debug flavour's PDBs grow past MSVC's limit on Windows**: incremental links grew
  `build-debug/openmw-rtxtool.pdb` to 6.4 GB, where the link failed with `LNK1318: Unexpected PDB error; LIMIT (12)`;
  `openmw.pdb` stood at 3.7 GB and `openmw-tests.pdb` at 3.4 GB beside it.
- **The translations lint finds no `lupdate` on Windows**: `./omw gate` stops at `omw: Qt's lupdate is not on the PATH
  nor in /usr/lib/qt6/bin, and the translations check needs it`, while the vcpkg set the build uses holds one at
  `out/deps/vcpkg-set/x64-windows-openmw/installed/x64-windows-openmw/tools/Qt6/bin/lupdate.exe` (`lint.py`'s
  `_lupdate`).
- **GPU tests run at the gate's time bound on a low-end AMD integrated GPU**: against the timing step's 2 s alone,
  `RtxVisibilityTest.theFloorAMovingBarUncoversStartsWithTheHistoryBesideIt` took 1.67 to 1.76 s run by itself and
  2.03 and 2.12 s in two `./omw gate` runs; `theVolumeLightsTheAirUpToASurfaceWhereverInASliceItStands` 1.57 and 2.03 s,
  and `theLampsLightIsExactOnAnOpenFloorAndTheirShadowsComeOutQuieter` 1.98 and 2.00 s, in the same two runs. Each
  run failed on them alone.
