# Open issues

- Two tests take about a second run alone in the debug build, with the validation layers loaded or not: `RtxRipplePassTest.theWakeKeepsItsPaceAtAnyFrameRate` 1.00 s, `RtxRipplePassTest.aDecayedFieldIsStillAndIsPressedAsStillWater` 0.98 s.
- `crash.matrix` failed once under `./omw gate` (debug) in its mode `ends-before-end`, "nothing says "Hang: the game ended before End was answered"": the log held "Hang: no frame for 2 seconds" after 3 seconds and 5 frames in its place. Run by itself three times after, `crash-tests --matrix` passed all 28 modes each time.
- Auto Exposure issue https://github.com/xorza/openmw-rtx/issues/52
