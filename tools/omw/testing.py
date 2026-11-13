"""`omw [flavour] test`: the suites `cmake/Tests.cmake` registers, through CTest, or one binary."""

import json
import shutil
from collections.abc import Callable
from functools import partial
from pathlib import Path

from omw.build import Build, properties_of
from omw.system import ROOT, Refusal, fork_files, read_text, status


def test(build: Build, args: list[str]) -> int:
    """Every suite the build has, the rest of the line to CTest, `--without-device` leaving out the GPU binary; or,
    named first, one binary built and run alone with the rest of the line, for a filter."""
    if args and not args[0].startswith("-"):
        return _one(build, args[0], args[1:])

    targets = build.test_targets(without_device="--without-device" in args)
    if not targets:
        raise Refusal(f"the {build.flavour} build has no tests: `omw debug test` runs them")
    build.build(targets)
    shutil.rmtree(times_folder(build), ignore_errors=True)
    # From the source tree, where CTest finds the presets.
    return status(build.run_here(["ctest", "--preset", build.preset, *ctest_arguments(args)],
                          cwd=ROOT).returncode)


def ctest_arguments(args: list[str]) -> list[str]:
    """The line in CTest's words, the suites side by side, a job a processor: here, as the preset takes only a
    fixed count."""
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
    return status(build.run_here([build.binary(binary), *args]).returncode)


# Two seconds, in the debug build: the fork's slowest test took 0.9 s alone, and a death test that starts the binary
# again and stands up a device about a quarter of a second each.
LIMIT_SECONDS = 2.0

# **The least of up to three runs alone**: what else the desk runs only ever slows a run, so the least of them is
# what a test costs (Chen, Revels and Edelman, "Robust benchmarking in noisy environments", 2016). On a low-end AMD
# integrated GPU one test took 1.67 to 2.12 s alone from one run to the next.
RUNS_ALONE = 3


def _alone_once(build: Build, binary: Path, case: str, report: Path, fork: frozenset[str]) -> float:
    """The seconds `case` of `binary` took run by itself, from the report it wrote to `report`."""
    build.run_here([binary, f"--gtest_filter={case}", f"--gtest_output=json:{report}"], check=True,
                   capture_output=True)
    return durations(json.loads(read_text(report)), fork)[case]


def least_alone(run: Callable[[], float], limit: float = LIMIT_SECONDS) -> float:
    """The least of up to `RUNS_ALONE` of `run`'s seconds, stopping at the first inside `limit`, which passes."""
    least = run()
    for _ in range(RUNS_ALONE - 1):
        if least <= limit:
            break
        least = min(least, run())
    return least


def times_folder(build: Build) -> Path:
    """Where each suite's GoogleTest report lands, one a CTest test, as `cmake/Tests.cmake` sets."""
    return build.folder("RTX_TEST_OUTPUT_DIR") / "times"


def durations(report: dict, fork: frozenset[str], root: Path = ROOT) -> dict[str, float]:
    """The fork's tests of one GoogleTest JSON report, those in a file of `fork` (`fork_files`), by full
    name, with the seconds each took. Upstream's are upstream's to change."""
    return {f"{suite['name']}.{case['name']}": float(case["time"].removesuffix("s"))
            for suite in report.get("testsuites", []) for case in suite.get("testsuite", [])
            if Path(case["file"]).is_relative_to(root)
            and Path(case["file"]).relative_to(root).as_posix() in fork}


def timing(build: Build) -> int:
    """**No test of the fork's takes longer than `LIMIT_SECONDS`, measured alone**: the last `test` run's reports
    name the candidates, which ran beside the others, and each runs again by itself, warm (`least_alone`)."""
    targets = {test["name"]: target for test in build.tests()
               if (target := properties_of(test).get("OPENMW_TARGET")) is not None}
    alone = build.folder("RTX_TEST_OUTPUT_DIR") / "alone.json"
    fork = fork_files()
    slow = 0
    for path in sorted(times_folder(build).glob("*.json")):
        for name, shared in durations(json.loads(read_text(path)), fork).items():
            if shared <= LIMIT_SECONDS:
                continue
            seconds = least_alone(partial(_alone_once, build, build.binary(targets[path.stem]), name, alone, fork))
            print(f"timing: {name} took {shared:.2f} s beside the other suites and {seconds:.2f} s alone")
            if seconds > LIMIT_SECONDS:
                slow += 1
    if slow:
        print(f"timing: {slow} test(s) over {LIMIT_SECONDS:g} s alone")
    return 1 if slow else 0
