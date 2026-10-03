"""`omw [flavour] test`: the suites `cmake/Tests.cmake` registers, through CTest, or one binary."""

import json
import shutil
from pathlib import Path

from omw.build import Build
from omw.system import FORK, ROOT, Refusal


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
    shutil.rmtree(times_folder(build), ignore_errors=True)
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


LIMIT_SECONDS = 1.0


def times_folder(build: Build) -> Path:
    """Where each suite's GoogleTest report lands, one a CTest test, as `cmake/Tests.cmake` sets."""
    return build.dir / "test-output" / "times"


def durations(report: dict, root: Path = ROOT) -> dict[str, float]:
    """The fork's tests of one GoogleTest JSON report, by full name, with the seconds each took. Upstream's
    are upstream's to change."""
    return {f"{suite['name']}.{case['name']}": float(case["time"].removesuffix("s"))
            for suite in report.get("testsuites", []) for case in suite.get("testsuite", [])
            if Path(case["file"]).is_relative_to(root)
            and Path(case["file"]).relative_to(root).as_posix().startswith(FORK)}


def timing(build: Build) -> int:
    """**No test of the fork's takes more than a second, measured alone.** Beside the other suites a test waits for the
    processors and the card they share, and the four that read past a second there took 0.35–0.96 s
    alone. So the last `test` run's reports name the candidates, and each runs again by itself, warm,
    where its time is its own: what is under the limit beside the others is under it alone."""
    targets = {test["name"]: property["value"] for test in build.tests()
               for property in test.get("properties", []) if property["name"] == "OPENMW_TARGET"}
    alone = build.dir / "test-output" / "alone.json"
    slow = 0
    for path in sorted(times_folder(build).glob("*.json")):
        for name, shared in durations(json.loads(path.read_text())).items():
            if shared <= LIMIT_SECONDS:
                continue
            build.run_here([build.binary(targets[path.stem]), f"--gtest_filter={name}",
                            f"--gtest_output=json:{alone}"], check=True, capture_output=True)
            seconds = durations(json.loads(alone.read_text()))[name]
            print(f"timing: {name} took {shared:.2f} s beside the other suites and {seconds:.2f} s alone")
            if seconds > LIMIT_SECONDS:
                slow += 1
    if slow:
        print(f"timing: {slow} test(s) over {LIMIT_SECONDS:g} s alone")
    return 1 if slow else 0
