"""`omw [flavour] test`: the suites `cmake/Tests.cmake` registers, through CTest, or one binary."""

from omw.build import Build
from omw.system import ROOT, Refusal


def test(build: Build, args: list[str]) -> int:
    """Every suite the build has. `--without-device` leaves out the GPU binary, which refuses to start
    without a device rather than skip; the rest of the line goes to CTest.

    Named first, a binary is built and run alone with the rest of the line, for a filter."""
    if args and not args[0].startswith("-"):
        return _one(build, args[0], args[1:])

    targets = build.test_targets()
    if not targets:
        raise Refusal(f"the {build.flavour} build has no tests: `omw debug test` runs them")
    build.build(targets)
    # From the source tree, where CTest finds the presets.
    return build.run_here(["ctest", "--preset", build.preset, *ctest_arguments(args)],
                          cwd=ROOT).returncode


def ctest_arguments(args: list[str]) -> list[str]:
    """The line in CTest's words. The suites run side by side, one job per processor: here, because the
    test preset's version takes only a fixed count."""
    passed: list[str] = ["--parallel"]
    for arg in args:
        if arg == "--without-device":
            passed += ["-LE", "device"]
        else:
            passed.append(arg)
    return passed


def _one(build: Build, binary: str, args: list[str]) -> int:
    if binary not in build.test_targets():
        raise Refusal(f"the {build.flavour} build has no test binary called {binary}")
    build.build([binary])
    return build.run_here([build.binary(binary), *args]).returncode
