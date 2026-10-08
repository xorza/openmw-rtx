# Open issues

- `tools/omw/listing.py` `check` reports `apps/components_tests/config/testlaunchersettings.cpp` as
  "tracked, and no list names it" in a build without Qt (`./omw asan test`): the test is added to
  `components-tests` only `if (USE_QT)`, and the check leaves out only `components_qt`'s sources.
- `VisibilityPass`'s single kernels — fog depth, sprite composite and sprite emitters — read the
  sky's variant constants (`SPEC_HAS_SUN`, `SPEC_HAS_MOONS`, `SPEC_HAS_SEA`) and are made once at
  the whole sky, whatever variant the frame traces under.
