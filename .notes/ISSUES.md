# Open issues

- `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` fails on a low-end AMD integrated GPU under AMD's Windows driver, at `7e2a45f48e`'s `sprites.glsl`: `water` holds 244 registers against 219, `bounce` 144 against 128, and `panes` 240 bytes of scratch against 192.
- `rtx.gpu.0` and `rtx.gpu.1` pass the 300 seconds of the test preset's timeout on a low-end AMD integrated GPU under AMD's Windows driver when the pipelines compile for the first time. The same binary, compiled already, takes 74 seconds.
- `crash.matrix` failed once on a low-end AMD integrated GPU under AMD's Windows driver, in `hang`, `recovers-before-end` and `ends-before-end`, beside GPU suites compiling their pipelines: the program paused about 15 seconds and reported a second hang, or wrote no package. Three runs of it alone passed.
- `RtxPenumbraTest.aFillCastsAPenumbraAsWideAsItsWholeBall` failed once with `2048 stores not finite in visibility.comp`, in a full `rtx-gpu-tests` run on a low-end AMD integrated GPU under AMD's Windows driver. Five repeats of the test alone passed.
