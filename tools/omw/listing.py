"""Every tracked source is one the build compiles: checked after `omw build` and in the gate.

A `CMakeLists.txt` lists files by base name, and a source no list names is never compiled, which
nothing else reports. So the compile database is read against `git ls-files`: every flavour builds
every program. Excused by rule: a file for another system (named `win32` or `posix` for the other one,
`linux` on Windows and `none` on Linux — the build of a facility only Linux has, and of its absence —
`stdio`, and `NOT_ON_WINDOWS` on Windows), and `ELSEWHERE`."""

import json
import os
from pathlib import PurePosixPath

from omw.build import Build
from omw.system import ROOT, WINDOWS, read_text, report, working_tree_files

ELSEWHERE = {
    # Writes `tablesgen.hpp`, built by the `Makefile` beside it.
    "components/toutf8/geniconv.cpp",
}

# Upstream's wizard unpacks the installer's archives with libunshield on every system but Windows,
# whose players run the installer itself, and the files keep upstream's names.
NOT_ON_WINDOWS = {
    "apps/wizard/installationpage.cpp",
    "apps/wizard/unshield/inisettings.cpp",
    "apps/wizard/unshield/unshieldworker.cpp",
}


def _other_system(name: str, windows: bool) -> bool:
    stem = PurePosixPath(name).stem
    others = ("posix", "linux") if windows else ("win32", "none")
    return stem.endswith((*others, "stdio")) or (windows and name in NOT_ON_WINDOWS)


def unlisted(tracked: list[str], compiled: set[str], windows: bool) -> list[str]:
    """The sources in `tracked` that `compiled`, a build on Windows or not, leaves out and no rule
    excuses; both are paths from the root, `/`-separated."""
    sources = [name for name in tracked if name.endswith(".cpp") and name.startswith(("apps/", "components/"))]
    return sorted(name for name in sources
                  if name not in compiled and name not in ELSEWHERE and not _other_system(name, windows))


# What `cmake/Checks.cmake` puts in every first-party command, on each compiler family: a flag
# `openmw-checks` always has and no third party's own build adds, as Google Benchmark adds `-Wextra`.
CHECKED = ("-Wzero-as-null-pointer-constant", "/w14855")


def third_party(name: str) -> bool:
    """Whether the source at `name`, from the tree's root, is a third party's: vendored under `extern/`,
    or fetched, into a build's `_deps/` or `fetched/` or outside the tree."""
    return name.startswith(("extern/", "../")) or os.path.isabs(name) or "/_deps/" in name or "/fetched/" in name


def misplaced_checks(database: list[dict], root: str) -> list[str]:
    """**The tree's checks reach its own sources and no third party's**: a source under `apps/` or
    `components/` whose command lacks them, and a third party's whose command has them. `root` is the tree's, as the database spells paths."""
    found = []
    for entry in database:
        name = os.path.relpath(entry["file"], root).replace(os.sep, "/")
        words = entry.get("arguments") or entry.get("command", "").split()
        checked = any(word in CHECKED for word in words)
        if name.startswith(("apps/", "components/")) and not checked:
            found.append(f"{name}: compiled without the tree's checks")
        elif third_party(name) and checked:
            found.append(f"{name}: a third party's, compiled with the tree's checks")
    return sorted(found)


def check(build: Build) -> int:
    tracked = working_tree_files("apps/*.cpp", "components/*.cpp")
    database = json.loads(read_text(build.dir / "compile_commands.json"))
    compiled = {os.path.relpath(entry["file"], ROOT).replace(os.sep, "/") for entry in database}
    return report([f"{name}: tracked, and no list names it" for name in unlisted(tracked, compiled, WINDOWS)]
                  + misplaced_checks(database, str(ROOT)))
