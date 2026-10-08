# Open issues

## The full flavour does not build under GCC 16.2: `-Wnull-dereference` in two tests

`omw full build` (`-O2 -g -Werror`) stops in `components-tests`:

- `apps/components_tests/rtx/mirror/shading.cpp:76` — "null pointer dereference" in
  `Shading::materialKey()` (`components/rtx/mirror/shading.hpp:89`), inlined through `chain.back()`
  of the `keyOf` lambda.
- `apps/components_tests/rtx/mirror/extractor/lights.cpp:438` — "potential null pointer dereference"
  in `StableIdentity::getId()` (`components/sceneutil/stableidentity.hpp:41`), on
  `StableIdentity::find(*stamped)->getId()` after its `ASSERT_NE`.

The gate stops at its build step on `master`, so its tests, `check` and `repeat` do not run.
