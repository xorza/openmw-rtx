"""Every tracked source is one the build compiles: checked after `omw build` and in the gate.

A `CMakeLists.txt` lists files by base name, and a source no list names is never compiled, which
nothing else reports. So the compile database is read against `git ls-files`. Excused by rule: a
program this flavour does not build, a file for another system (named `win32` or `posix` for the
other one, `stdio`, or `android…`, and `NOT_ON_WINDOWS` on Windows), and `ELSEWHERE`."""

import json
import os
from pathlib import PurePosixPath

from omw.build import Build
from omw.system import ROOT, WINDOWS, output

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
    return (stem.endswith(("posix" if windows else "win32", "stdio")) or stem.startswith("android")
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


def check(build: Build) -> int:
    # The working tree's files: an unstaged deletion still stands in the index.
    listed = output(["git", "-C", ROOT, "ls-files", "--cached", "--others", "--exclude-standard", "--",
                     "apps/*.cpp", "components/*.cpp"]).splitlines()
    tracked = [name for name in listed if (ROOT / name).is_file()]
    database = json.loads((build.dir / "compile_commands.json").read_text())
    # Without Qt a build leaves out libraries no rule can name; debug, full and package have Qt.
    if not any("components_qt.dir" in entry.get("output", "") for entry in database):
        return 0
    compiled = {PurePosixPath(os.path.relpath(entry["file"], ROOT).replace(os.sep, "/")).as_posix()
                for entry in database}
    missing = unlisted(tracked, compiled, WINDOWS)
    for name in missing:
        print(f"{name}: tracked, and no list names it")
    return 1 if missing else 0
