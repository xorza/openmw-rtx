"""Every tracked source is one the build compiles: checked after `omw build` and in the gate.

A `CMakeLists.txt` lists files by base name, and a source no list names is never compiled, which
nothing else reports. So the compile database is read against `git ls-files`. Excused by rule: a
program this flavour does not build, a file for another system (named `win32` or `posix` for the
other one, `linux` on Windows and `none` on Linux — the build of a facility only Linux has, and of
its absence — `stdio`, or `android…`, and `NOT_ON_WINDOWS` on Windows), what only Qt builds in a
build without it, and `ELSEWHERE`."""

import json
import os
import re
from pathlib import PurePosixPath

from omw.build import Build
from omw.system import ROOT, WINDOWS, read_text, working_tree_files

ELSEWHERE = {
    # Writes `tablesgen.hpp`, built by the `Makefile` beside it.
    "components/toutf8/geniconv.cpp",
    # The catcher of a build with `OPENMW_CRASHPAD` off.
    "components/crashcatcher/crashunsupported.cpp",
}

# Upstream's wizard unpacks the installer's archives with libunshield on every system but Windows,
# whose players run the installer itself, and the files keep upstream's names.
NOT_ON_WINDOWS = {
    "apps/wizard/installationpage.cpp",
    "apps/wizard/unshield/inisettings.cpp",
    "apps/wizard/unshield/unshieldworker.cpp",
}


def _program(name: str) -> str:
    return "/".join(name.split("/")[:2])


def _other_system(name: str, windows: bool) -> bool:
    stem = PurePosixPath(name).stem
    others = ("posix", "linux") if windows else ("win32", "none")
    return (stem.endswith((*others, "stdio")) or stem.startswith("android")
            or (windows and name in NOT_ON_WINDOWS))


def unlisted(tracked: list[str], compiled: set[str], windows: bool) -> list[str]:
    """The sources in `tracked` that `compiled`, a build on Windows or not, leaves out and no rule
    excuses; both are paths from the root, `/`-separated."""
    sources = [name for name in tracked if name.endswith(".cpp") and name.startswith(("apps/", "components/"))]
    built_apps = {_program(name) for name in compiled if name.startswith("apps/")}
    return sorted(name for name in sources
                  if name not in compiled
                  and name not in ELSEWHERE
                  and not _other_system(name, windows)
                  and not (name.startswith("apps/") and _program(name) not in built_apps))


def qt_sources(cmake_text: str) -> set[str]:
    """The sources of `components_qt`, read off the `add_component_qt_dir` lists of
    `components/CMakeLists.txt`, the one place that names them: what a build without Qt leaves out."""
    found: set[str] = set()
    for folder, names in re.findall(r"^\s*add_component_qt_dir\s*\(\s*(\S+)(.*?)\)", cmake_text,
                                    re.MULTILINE | re.DOTALL):
        found |= {f"components/{folder}/{name}.cpp" for name in names.split()}
    return found


def qt_guarded_sources(cmake_text: str, folder: str) -> set[str]:
    """The sources a list in `folder` adds to a target only `if (USE_QT)`, through `target_sources`:
    what a build without Qt leaves out of a program it builds, as `components-tests` leaves out the
    launcher's settings test."""
    # By depth, so an `if` nested inside the block does not end it at its own `endif`, and up to the
    # block's own `else`, whose branch is the build without Qt.
    guarded: list[str] = []
    depth = 0
    otherwise = False
    for line in cmake_text.splitlines():
        if depth == 0:
            depth = 1 if re.match(r"\s*if\s*\(\s*USE_QT\s*\)", line) else 0
            otherwise = False
            continue
        if re.match(r"\s*if\s*\(", line):
            depth += 1
        elif re.match(r"\s*endif\s*\(", line):
            depth -= 1
        elif depth == 1 and re.match(r"\s*else(if)?\s*\(", line):
            otherwise = True
        if depth > 0 and not otherwise:
            guarded.append(line)

    found: set[str] = set()
    for names in re.findall(r"target_sources\s*\(\s*\S+\s+(?:PRIVATE|PUBLIC|INTERFACE)\s+([^)]*)\)",
                            "\n".join(guarded)):
        found |= {f"{folder}/{name}" for name in names.split() if name.endswith(".cpp")}
    return found


def check(build: Build) -> int:
    tracked = working_tree_files("apps/*.cpp", "components/*.cpp")
    database = json.loads(read_text(build.dir / "compile_commands.json"))
    compiled = {os.path.relpath(entry["file"], ROOT).replace(os.sep, "/") for entry in database}
    # A build without Qt — asan, tsan, release, and debug on Windows — leaves `components_qt` out,
    # and what a program's list adds only with Qt.
    if not any("components_qt.dir" in entry.get("output", "") for entry in database):
        qt_only = qt_sources(read_text(ROOT / "components" / "CMakeLists.txt"))
        for listed in sorted((ROOT / "apps").glob("*/CMakeLists.txt")):
            qt_only |= qt_guarded_sources(read_text(listed), listed.parent.relative_to(ROOT).as_posix())
        tracked = [name for name in tracked if name not in qt_only]
    missing = unlisted(tracked, compiled, WINDOWS)
    for name in missing:
        print(f"{name}: tracked, and no list names it")
    return 1 if missing else 0
