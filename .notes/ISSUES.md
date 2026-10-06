# Open issues

- `RtxVisibilityTest.aLampLightsTheAirItStandsInByTheIsotropicShareOfWhatItDelivers`
  (`apps/components_tests/rtxvulkan/trace/visibility/fog.cpp`) compares the lamp behind the lid,
  `look(true, true)`, with a frame that has no lid, `look(false)`. The lid blocks part of the wall's
  sky, and the wall's light at the centre pixel is one bounce sample, so the two agree only while
  that pixel's bounce draw at frame 0 misses the lid. Another draw for the bounce's pair made the
  test fail at 0 against 108.

- The fog volume's history images (`BIND_FOG_WAS_SCATTER`, `BIND_FOG_WAS_SUNWARD`,
  `FOG_VOLUME_FORMAT` = `RGBA16F`) are read back into their own blend (`FOG_VOLUME_HISTORY`) every
  frame and stored in halves, whose store this card rounds toward nought (`RtxHalfStoreTest`):
  the history `DenoiseHistory`'s fed-back rule refuses for the denoiser's images.

- `RtxBounceTrailTest.theClampShortensTheSkysTrail`
  (`apps/components_tests/rtxvulkan/trace/visibility/trail.cpp`) takes 1.3 s in the debug build,
  over the one-second bound for a single test.

- The sprite bin's zone (`sprites`) at `balmora-storm-night` has a median of 0.05 ms and a p99 of
  0.6 to 1.3 ms in every release bench run, with a worst of 1.1 to 1.7 ms: a few frames in a hundred
  pay twenty times the bin's usual cost.

- `RtxNvmlTest.theLibraryAnswersAClockAndNamesThisProcess`
  (`apps/components_tests/rtxtool/cardwatch.cpp:150`) fails on Windows: the name NVML gives this
  process is `components-tests.exe`, and the test expects `components-tests`.
