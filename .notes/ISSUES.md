# Open issues

- `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn` takes 1.02 s run alone in the debug build on the low-end AMD integrated GPU under Windows (the gate's timing step).
- On NVIDIA, 47 of the 334 pipelines a start makes on the shipped shaders spill to scratch memory, up to 112 bytes a lane: 34 `panes` tuples (`panes sun sea maps`, `panes moons sea maps relief` and others at 112), 10 `direct` and 2 `media`. `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` holds only the full tuple and the one without a dark map's alpha, so the specialized tuples that spill are not held.
