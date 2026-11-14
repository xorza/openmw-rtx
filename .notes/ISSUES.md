# Open issues

- `RtxPresenterTest.aLostSurfaceIsMadeAgainAndTheFramesGoOn` takes 1.02 s run alone in the debug build on the low-end AMD integrated GPU under Windows (the gate's timing step).
- On NVIDIA every start's log warns that 47 pipelines use scratch memory (`PipelineReport::logSummary`), the local memory `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` holds at its ceilings.
- `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` holds AMD's driver's and RADV's tuples at the figures of the two tuples measured there, the whole one and the one without a dark map's alpha: the other 46 have not been run on either driver.
