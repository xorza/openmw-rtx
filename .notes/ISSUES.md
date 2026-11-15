# Open issues

- Unscoped enums: `components/rtx/frame/framepast.hpp:40`, `apps/openmw/mwrender/rtx/worldmirror.cpp:163`, `components/crashcatcher/crashpadmonitor.cpp:376`.
- `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` holds RADV's tuples at the figures of the two tuples measured there, the whole one and the one without a dark map's alpha: the other 46 have not been run under RADV.
- `omw.tests.test_tidycache.DependenciesTest.test_every_prerequisite_made_plain_and_absolute_and_the_target_left_out` fails on Windows: `tidycache.dependencies` answers `C:\src\a.cpp` for a prerequisite `/src/a.cpp` where the test expects `\src\a.cpp`, so `omw gate` stops at the driver's tests.
- `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` holds NVIDIA's tuples that read a mod's maps (`HAS_MOD_MAPS`) at rows measured before they existed: those tuples have not been run under NVIDIA's driver.
- `omw gate`'s clang-tidy step fails over the whole tree on Windows: 283 findings in 207 sources, mostly `misc-include-cleaner` naming `<GL>` and `bugprone-exception-escape`.
