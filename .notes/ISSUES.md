# Open issues

- A test that collects a report itself (`finishFrame` or `collectFrame`) and does not read its `mNotFinite`, as `trail.cpp`, `framecost.cpp`, `frames.cpp`, `filter.cpp`, `surfaces.cpp`, `upscale.cpp` and `frame.cpp` do outside `shoot`, takes the frame's non-finite stores out of the ring, and no check sees them: `RendererTest::TearDown` reports only the stores of reports nothing collected.
