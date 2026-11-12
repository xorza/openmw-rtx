# Open issues

- **`--pipeline-code` ends a run on AMD's Windows driver**: on a low-end AMD integrated GPU under AMD's Windows driver
  26.8.1, `openmw-rtxtool shot --pipeline-code=<dir>` stops with `vkGetPipelineExecutableInternalRepresentationsKHR
  failed: VK_INCOMPLETE (5), while compiling the pipeline "sprite emitters"` (`Device::reportPipeline`).
- **The translations lint finds no `lupdate` on Windows**: `./omw gate` stops at `omw: Qt's lupdate is not on the PATH
  nor in /usr/lib/qt6/bin, and the translations check needs it`, while the vcpkg set the build uses holds one at
  `out/deps/vcpkg-set/x64-windows-openmw/installed/x64-windows-openmw/tools/Qt6/bin/lupdate.exe` (`lint.py`'s
  `_lupdate`).
- **GPU tests run at the gate's time bound on a low-end AMD integrated GPU**: against the timing step's 2 s alone,
  `RtxVisibilityTest.theFloorAMovingBarUncoversStartsWithTheHistoryBesideIt` took 1.67 to 1.76 s run by itself and
  2.03 and 2.12 s in two `./omw gate` runs; `theVolumeLightsTheAirUpToASurfaceWhereverInASliceItStands` 1.57 and 2.03 s,
  and `theLampsLightIsExactOnAnOpenFloorAndTheirShadowsComeOutQuieter` 1.98 and 2.00 s, in the same two runs. Each
  run failed on them alone.
- **`SceneUtil::Material`'s `.osgt` wrapper is registered only by `SceneUtil::writeScene`**
  (`SceneUtil::registerSerializers`): a model the game loads that names `SceneUtil::Material` loses its material, and
  the log says `InputStream::readObject(): Unsupported wrapper class SceneUtil::Material`. The embedded error marker
  (`Resource::ErrorMarker::sValue`) names one.
