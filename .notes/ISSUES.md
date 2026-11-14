# Open issues

- Unscoped enums: `components/rtx/frame/framepast.hpp:40`, `apps/openmw/mwrender/rtx/worldmirror.cpp:163`, `components/crashcatcher/crashpadmonitor.cpp:376`.
- 61 includes in the fork's production sources name nothing the file uses (101 with the tests and benchmarks), e.g. `apps/rtxtool/cameradriver.cpp:35` (`skylight.hpp`) and `apps/openmw/mwrender/renderer.cpp:6-8` (`<exception>`, `<stdexcept>`, `<string>`): what `misc-include-cleaner` reports with `MissingIncludes: false`.
- `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` holds RADV's tuples at the figures of the two tuples measured there, the whole one and the one without a dark map's alpha: the other 46 have not been run under RADV.
