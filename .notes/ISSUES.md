# Open issues

- Two tests take about a second run alone in the debug build, with the validation layers loaded or not: `RtxRipplePassTest.theWakeKeepsItsPaceAtAnyFrameRate` 1.00 s, `RtxRipplePassTest.aDecayedFieldIsStillAndIsPressedAsStillWater` 0.98 s.
- Two tests of `components-tests` fail in the debug build under MSVC at `e21fad4c21`, so `./omw gate` stops at its tests there: `RtxFrameTimeHistogramTest.aSessionIsSummarisedToATenthOfAMillisecond` describes the worst frame as "(frame 300.0 finish 0.0 wait 0.0)" where it expects "(update 250.0 place 30.0 walk 15.0)", the first three of `Timing`'s order and the whole frame among them in place of the three longest stretches, and `RtxBenchRecordTest.arrivalsCountExtendingFramesAndKeepTheThreeWorst` finds no "9.0 ms (12 meshes: update 4.0 place 2.0 finish 1.5)" in the record it describes.
- Auto Exposure issue https://github.com/xorza/openmw-rtx/issues/52
