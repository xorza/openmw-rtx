# Open issues

- A settled history of a rare bright bounce filters to about half its light: in
  `theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo`'s scene (a floor whose bounce now and then finds a
  lamp-lit spot on a wall), the strip held still for 32 frames reads 0.56 of 128 unfiltered frames'
  mean, with the variance before step 16 and after it.
- The trail tests (`apps/components_tests/rtxvulkan/trace/visibility/trail.cpp`) move their bar with
  `placements().move` and hand it over with `placeScene`, and never call `placements().advance()`,
  which `SceneUploader` calls after every hand-over: the bar's motion vector on each frame is its
  whole travel since it stood still (22 pixels where a frame's step is 5.5, in the same setup), so
  the trails they measure are of a reprojection the game never makes.
