"""`omw [flavour] test`: the suites `cmake/Tests.cmake` registers, through CTest, or one binary."""

from omw.build import Build
from omw.system import ROOT, Refusal


def test(build: Build, args: list[str]) -> int:
    """**The fork's tests by default**, which are what a change here moves: the `fork` label, the
    crash matrix and the GPU binary among them. `--all` adds upstream's, which the plain flavour
    always runs, being upstream's own build. **`--without-device` leaves the GPU binary out**, for a
    box with no driver: the binary refuses to start without a device, so a run that says nothing
    about its device is one that has one. What else the line holds goes to CTest as it is.

    Named first, a binary is built and run alone with the rest of the line, for a filter."""
    if args and not args[0].startswith("-"):
        return _one(build, args[0], args[1:])

    targets = build.test_targets()
    if not targets:
        raise Refusal(f"the {build.flavour} build has no tests: `omw debug test` runs them")
    build.build(targets)
    # From the source tree, where CTest finds the presets.
    return build.run_here(["ctest", "--preset", build.preset, *ctest_arguments(build.flavour, args)],
                          cwd=ROOT).returncode


def ctest_arguments(flavour: str, args: list[str]) -> list[str]:
    """The line's own switches in CTest's words, and the rest as it is. **The suites run side by
    side**, one job to a processor: here and not in the test preset, whose version takes only a fixed
    count."""
    whole = flavour == "plain"
    passed: list[str] = ["--parallel"]
    for arg in args:
        if arg == "--all":
            whole = True
        elif arg == "--without-device":
            passed += ["-LE", "device"]
        else:
            passed.append(arg)
    if not whole:
        passed += ["-L", "fork"]
    return passed


def _one(build: Build, binary: str, args: list[str]) -> int:
    if binary not in build.test_targets():
        raise Refusal(f"the {build.flavour} build has no test binary called {binary}")
    build.build([binary])
    return build.run_here([build.binary(binary), *args]).returncode
