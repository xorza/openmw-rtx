"""`omw [flavour] test`: the suites `cmake/Tests.cmake` registers, through CTest, or one binary."""

import json
import shutil
from collections.abc import Iterable
from itertools import chain
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

# Thirty seconds a suite, one CTest test: a GoogleTest binary, or one shard of the GPU binary, which `cmake/Tests.cmake`
# cuts so that no shard runs past it. The slowest shard ran 12.5 s on a high-end NVIDIA laptop GPU and 27.5 s at its
# fastest of three runs on a low-end AMD integrated GPU (`docs/research/2026-11-28-gpu-tests-validation-cost.md`).
SUITE_LIMIT_SECONDS = 30.0

# **The least of up to three runs**: what else the desk runs only ever slows a run, so the least of them is what a test
# or a suite costs (Chen, Revels and Edelman, "Robust benchmarking in noisy environments", 2016). On a low-end AMD
# integrated GPU one test took 1.67 to 2.12 s alone from one run to the next, and the first test run after a shader
# change, which compiles every pipeline, ran its suites at 42 to 84 s against 23 to 32 s in the run after it.
RUNS = 3


def _alone_once(build: Build, binary: Path, case: str, report: Path, fork: frozenset[str]) -> float:
    """The seconds `case` of `binary` took run by itself, from the report it wrote to `report`."""
    build.run_here([binary, f"--gtest_filter={case}", f"--gtest_output=json:{report}"], check=True,
                   capture_output=True)
    return durations(json.loads(read_text(report)), fork)[case]


def least_of_runs(runs: Iterable[dict[str, float]], limit: float) -> dict[str, float]:
    """Each name's least seconds over up to `RUNS` of `runs`, taken one at a time, stopping once every name's least
    is inside `limit`, which passes. Every run times the names the first one did."""
    taken = iter(runs)
    least = next(taken)
    for _ in range(RUNS - 1):
        if all(seconds <= limit for seconds in least.values()):
            break
        again = next(taken)
        least = {name: min(seconds, again[name]) for name, seconds in least.items()}
    return least


def times_folder(build: Build) -> Path:
    """Where each suite's GoogleTest report lands, one a CTest test, as `cmake/Tests.cmake` sets."""
    return build.folder("RTX_TEST_OUTPUT_DIR") / "times"


def suite_seconds(report: dict) -> float:
    """The seconds one GoogleTest JSON report's whole run took, nought where it says none."""
    return float(str(report.get("time", "0s")).removesuffix("s"))


def durations(report: dict, fork: frozenset[str], root: Path = ROOT) -> dict[str, float]:
    """The fork's tests of one GoogleTest JSON report, those in a file of `fork` (`fork_files`), by full
    name, with the seconds each took. Upstream's are upstream's to change."""
    return {f"{suite['name']}.{case['name']}": float(case["time"].removesuffix("s"))
            for suite in report.get("testsuites", []) for case in suite.get("testsuite", [])
            if Path(case["file"]).is_relative_to(root)
            and Path(case["file"]).relative_to(root).as_posix() in fork}


def _suites_of_last_run(build: Build) -> dict[str, float]:
    """Each suite's seconds, by its CTest name, from the reports the last `test` run wrote."""
    return {path.stem: suite_seconds(json.loads(read_text(path)))
            for path in sorted(times_folder(build).glob("*.json"))}


def _suites_run_again(build: Build) -> dict[str, float]:
    """Each suite's seconds from the tests run again as `test` runs them, side by side: a suite run alone would have
    the processors and the device to itself, which no run of the default command gives it."""
    print(f"timing: a suite ran past {SUITE_LIMIT_SECONDS:g} s, so the tests run again and each suite counts its "
          "least run")
    if test(build, []) != 0:
        raise Refusal("a test failed in a run made to time the suites, after the same tests passed")
    return _suites_of_last_run(build)


def timing(build: Build) -> int:
    """**No test of the fork's takes longer than `LIMIT_SECONDS`, measured alone, and no suite longer than
    `SUITE_LIMIT_SECONDS`**, each by its least run (`least_of_runs`): a suite past its bound in the last `test` run
    runs again with the others, and the last run's reports name the tests to run again by themselves."""
    suites = least_of_runs(chain([_suites_of_last_run(build)], (_suites_run_again(build) for _ in range(RUNS - 1))),
                           SUITE_LIMIT_SECONDS)
    slow = 0
    for name, suite in suites.items():
        if suite > SUITE_LIMIT_SECONDS:
            print(f"timing: the suite {name} took {suite:.1f} s at its least, past {SUITE_LIMIT_SECONDS:g} s")
            slow += 1

    targets = {entry["name"]: target for entry in build.tests()
               if (target := properties_of(entry).get("OPENMW_TARGET")) is not None}
    alone = build.folder("RTX_TEST_OUTPUT_DIR") / "alone.json"
    fork = fork_files()
    for path in sorted(times_folder(build).glob("*.json")):
        binary = build.binary(targets[path.stem])
        for name, shared in durations(json.loads(read_text(path)), fork).items():
            if shared <= LIMIT_SECONDS:
                continue
            seconds = least_of_runs(({name: _alone_once(build, binary, name, alone, fork)} for _ in range(RUNS)),
                                    LIMIT_SECONDS)[name]
            print(f"timing: {name} took {shared:.2f} s beside the other suites and {seconds:.2f} s alone")
            if seconds > LIMIT_SECONDS:
                slow += 1
    if slow:
        print(f"timing: {slow} test(s) or suite(s) over their bound")
    return 1 if slow else 0
