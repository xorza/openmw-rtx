"""`omw lint [check...]`: the checks CI runs besides the formatting, run by the gate through the same code: upstream's
file names and Qt translations, every header's `#pragma once`, the fork's lint of the driver and audit of the
workflows. CI's `checks` job runs all but the translations, which take the vcpkg set's Qt and so run in a leg that
holds the set.

**Ruff, mypy and zizmor are the driver's own** (`deps`), fetched at the versions and digests `pins.py` names and
never installed: one of each on every desk and CI, and no part of a check is whatever an index serves that day."""

import filecmp
import os
import re
import shutil
import subprocess
import tempfile
from collections.abc import Callable
from pathlib import Path

from omw import deps
from omw.system import CXX_SOURCES, OUT, ROOT, SYSTEMS, Refusal, read_text, report, working_tree_files

NAMED = re.compile(r"/[a-z0-9]+\.(cpp|hpp|h)$")


def misnamed(names: list[str]) -> list[str]:
    """Each source of `names` upstream's convention refuses: lower case and digits alone, in a folder."""
    return [f"{name}: a source is named in lower case and digits alone" for name in names if not NAMED.search(name)]


def _names() -> list[str]:
    """Upstream's rule for the names of sources (`misnamed`)."""
    return misnamed(working_tree_files(*CXX_SOURCES))


# The headers GLSL reads as well, which keep the guards `portable.h` says GLSL needs.
GLSL_HEADERS = ("components/rtx/shaders/", "components/rtxvulkan/shaders/shared/")
DIRECTIVE = re.compile(r"^\s*#")


def unguarded(headers: dict[str, str]) -> list[str]:
    """Each header of `headers`, by name and text, whose first directive is not `#pragma once`: the tree's one
    spelling of a guard, which `.clang-tidy` lets stand."""
    found = []
    for name, text in headers.items():
        first = next((line.strip() for line in text.splitlines() if DIRECTIVE.match(line)), None)
        if first != "#pragma once":
            found.append(f"{name}: a header opens with `#pragma once`, not {first or 'no directive'}")
    return found


def _pragmas() -> list[str]:
    """Every header of apps/ and components/ but GLSL's guarded once (`unguarded`)."""
    names = working_tree_files("apps/*.hpp", "apps/*.h", "components/*.hpp", "components/*.h")
    return unguarded({name: read_text(ROOT / name) for name in names if not name.startswith(GLSL_HEADERS)})


# The sources each set of translations is read out of.
TRANSLATIONS = (
    (["apps/wizard"], "wizard_*.ts"),
    (["apps/launcher"], "launcher_*.ts"),
    (["components/contentselector", "components/process"], "components_*.ts"),
)


def _lupdate() -> str:
    """**Qt's `lupdate` of the vcpkg set**, the Qt whose `lrelease` the build compiles the same `.ts` files with
    (`qt_add_lrelease`): another Qt's can write a file differently and flag one the build reads as it stands."""
    return str(deps.set_tool("Qt6/bin", "lupdate"))


def _regenerate(lupdate: str, sources: list[str], ts_files: list[Path], purge: bool) -> None:
    """lupdate over `sources` into `ts_files`, as both the check and `omw translations` run it; `purge` drops the
    strings no source has."""
    subprocess.run([lupdate, "-silent", "-locations", "none", *(["-no-obsolete"] if purge else []), *sources,
                    "-ts", *map(str, ts_files)], cwd=ROOT, check=True)


def _translations() -> list[str]:
    """The translations regenerated from their sources do not change, **on copies**, so a tree with other changes
    still passes."""
    lupdate = _lupdate()
    found = []
    with tempfile.TemporaryDirectory(prefix="omw-lint-") as scratch:
        for sources, pattern in TRANSLATIONS:
            originals = sorted((ROOT / "files" / "lang").glob(pattern))
            copies = [Path(scratch) / original.name for original in originals]
            for original, copy in zip(originals, copies, strict=True):
                shutil.copyfile(original, copy)
            _regenerate(lupdate, sources, copies, purge=False)
            found += [f"{original.relative_to(ROOT).as_posix()}: out of date with its sources; run "
                      "`omw translations`" for original, copy in zip(originals, copies, strict=True)
                      if not filecmp.cmp(original, copy, shallow=False)]
    return found


def translations(args: list[str]) -> int:
    """`omw translations [--purge]`: the `.ts` files regenerated in place, the strings no source has kept as
    obsolete, or with `--purge` dropped."""
    if args not in ([], ["--purge"]):
        raise Refusal("translations takes `--purge` or nothing")
    lupdate = _lupdate()
    for sources, pattern in TRANSLATIONS:
        _regenerate(lupdate, sources, sorted((ROOT / "files" / "lang").glob(pattern)), purge=bool(args))
    return 0


def _ran(what: str, command: list[str], env: dict[str, str] | None = None) -> list[str]:
    """A tool that prints its own findings: nothing where it passed, and a line that names it, `what`, where it did
    not. By its name and not its command, which for a tool run as a module is a line of Python."""
    code = subprocess.run(command, cwd=ROOT, env=env, check=False).returncode
    return [] if code == 0 else [f"{what} exited with {code}"]


def _driver() -> list[str]:
    """Ruff by `ruff.toml`, which an editor reads too, and mypy, against the oldest Python `omw` supports. **Mypy
    once for each system the driver builds on**, with `WINDOWS` a constant to it, so each system's branches are
    checked against that system's `os`, and with a cache of that system's own: one cache between the two had each
    run check again what the other left."""
    env = {**os.environ, "MYPYPATH": str(ROOT / "tools")}
    found = _ran("ruff", [str(deps.ruff()), "check", "--config", "tools/omw/ruff.toml", "tools/omw", "omw"])
    mypy = deps.mypy()
    for platform in SYSTEMS:
        windows = "true" if platform == "win32" else "false"
        found += _ran(f"mypy for {platform}",
                      [*mypy, "--python-version", "3.11", "--platform", platform, f"--always-{windows}", "WINDOWS",
                       "--cache-dir", str(OUT / "mypy" / platform), "-p", "omw"], env)
    return found


# The image every Linux build runs in, pinned by digest in one place: the release base's `FROM`.
RELEASE_BASE_FILE = Path("env") / "release-base.Dockerfile"


def _workflows() -> list[str]:
    """zizmor, offline, and every workflow's container the release base's image: a finding is a failure."""
    return _containers(ROOT) + _ran("zizmor", [str(deps.zizmor()), "--offline", "."])


def _containers(root: Path) -> list[str]:
    """**Each `container:` a workflow names is the release base's image, digest and all**, so CI builds on the
    image a desk's container does. One chosen by an expression is held where the matrix names it."""
    base = re.search(r"^FROM\s+(\S+)", read_text(root / RELEASE_BASE_FILE), re.MULTILINE)
    assert base is not None, f"{RELEASE_BASE_FILE} names no image"
    image = base.group(1)
    found = []
    for workflow in sorted((root / ".github" / "workflows").glob("*.yml")):
        for number, line in enumerate(read_text(workflow).splitlines(), 1):
            named = re.match(r"\s*(?:-\s+)?container:\s*(\S+)\s*$", line)
            if named and not named.group(1).startswith("${{") and named.group(1) != image:
                found.append(f"{workflow.relative_to(root).as_posix()}:{number}: container {named.group(1)} "
                             f"is not {RELEASE_BASE_FILE.as_posix()}'s {image}")
    return found


CHECKS: dict[str, Callable[[], list[str]]] = {
    "names": _names,
    "pragmas": _pragmas,
    "translations": _translations,
    "driver": _driver,
    "workflows": _workflows,
}


def lint(args: list[str]) -> int:
    """The checks named, or every one, each run whatever the one before found and its findings printed as it
    ends."""
    unknown = [name for name in args if name not in CHECKS]
    if unknown:
        raise Refusal(f"no check is called {', '.join(unknown)}: {', '.join(CHECKS)}")
    statuses = [report(CHECKS[name]()) for name in args or list(CHECKS)]
    return max(statuses, default=0)
