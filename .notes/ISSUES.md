# Open issues

- `RtxThreadCountersTest.aLoopTwiceAsLongCountsTwiceTheInstructions` failed once in a gate with a ratio of 1.868 against 2.0 and a tolerance of 0.1, with no change near it, and passes alone.

- Regenerating `build-release/build.ninja` during `./omw gate` failed once with `volk::volk_headers` not found at `components/rtxvulkan/CMakeLists.txt:573` and `:594`; `./omw release build` then passed with no change.
