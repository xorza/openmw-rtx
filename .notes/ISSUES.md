# Open issues

- Two tests take about a second run alone in the debug build, with the validation layers loaded or not: `RtxRipplePassTest.theWakeKeepsItsPaceAtAnyFrameRate` 1.00 s, `RtxRipplePassTest.aDecayedFieldIsStillAndIsPressedAsStillWater` 0.98 s.
- `RtxTool::ShippedDefaults` constructed from a `defaults.bin` that is empty asserts in `std::string::operator[]` (`_GLIBCXX_ASSERTIONS`) instead of refusing the file.
- `omw noise --ab=<switch> --out=<dir>` with a relative `<dir>`: the harness, which runs from the build directory, writes each leg under the build directory, and the driver then looks for the leg's record against its own working directory and reports the leg failed (`tools/omw/noise.py`, `ab`).
- Under a host that runs the game clock itself (`film --clock`), every frame is told to the renderer as a cut ("History reset by a cut" each frame): the host advances the clock in `beforeFrame`, then the engine's held `World::advanceTime(0.0, true)` sets `mHourStep` to nought, so `HourWatch` reads the host's advance as a jump.
