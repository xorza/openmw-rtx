"""`omw lint [check...]`: the checks CI's `checks` job runs besides the formatting, run by the gate through the same
code: upstream's file names and Qt translations, the fork's lint of the driver and audit of the workflows.

**The pins are here and nowhere else**: the desk runs the pinned tool from the PATH and refuses another version; CI
runs the pin through `pipx run`, which the desk never does unasked."""

import filecmp
import os
import re
import shutil
import subprocess
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from omw.system import (
    CI,
    CXX_SOURCES,
    OUT,
    ROOT,
    SYSTEMS,
    Refusal,
    on_path,
    read_text,
    report,
    require,
    working_tree_files,
)


@dataclass(frozen=True)
class Pinned:
    """A Python tool CI runs at one version."""
    name: str
    version: str


RUFF = Pinned("ruff", "0.17.4")
MYPY = Pinned("mypy", "2.4.1")
ZIZMOR = Pinned("zizmor", "1.32.0")


def version_of(said: str) -> str | None:
    """The version a tool's `--version` printed, its second word: `mypy 2.3.1 (compiled: yes)`."""
    words = said.split()
    return words[1] if len(words) > 1 else None


def command_for(tool: Pinned, ci: bool = CI) -> list[str]:
    """How `tool` at its pin is started: from the PATH where the version there is the pin, through `pipx run` on
    CI, and otherwise refused with the command that installs it."""
    found = on_path(tool.name)
    said = "" if found is None else subprocess.run([found, "--version"], capture_output=True, text=True,
                                                   check=False).stdout.strip()
    if found is not None and version_of(said) == tool.version:
        return [found]
    if ci:
        return [require("pipx", f"CI runs {tool.name} {tool.version} through it"), "run",
                f"{tool.name}=={tool.version}"]
    there = "is not on the PATH" if found is None else f"on the PATH is {said or 'of no version'}"
    raise Refusal(f"{tool.name} {tool.version}, the version CI pins, {there}: "
                  f"`pipx install {tool.name}=={tool.version}`")


NAMED = re.compile(r"/[a-z0-9]+\.(cpp|hpp|h)$")


def misnamed(names: list[str]) -> list[str]:
    """Each source of `names` upstream's convention refuses: lower case and digits alone, in a folder."""
    return [f"{name}: a source is named in lower case and digits alone" for name in names if not NAMED.search(name)]


def _names() -> list[str]:
    """Upstream's rule for the names of sources (`misnamed`)."""
    return misnamed(working_tree_files(*CXX_SOURCES))


# The sources each set of translations is read out of.
TRANSLATIONS = (
    (["apps/wizard"], "wizard_*.ts"),
    (["apps/launcher"], "launcher_*.ts"),
    (["components/contentselector", "components/process"], "components_*.ts"),
)


def _lupdate() -> str:
    """Qt's `lupdate`: where `LUPDATE` says, on the PATH, or where some distributions keep Qt 6's tools off it."""
    for candidate in (os.environ.get("LUPDATE"), on_path("lupdate"), "/usr/lib/qt6/bin/lupdate"):
        if candidate and Path(candidate).is_file():
            return candidate
    raise Refusal("Qt's lupdate is not on the PATH nor in /usr/lib/qt6/bin, and the translations check needs it")


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


def _ran(command: list[str], env: dict[str, str] | None = None) -> list[str]:
    """A tool that prints its own findings: nothing where it passed, and the line that names it where it did not."""
    code = subprocess.run(command, cwd=ROOT, env=env, check=False).returncode
    return [] if code == 0 else [f"`{' '.join(command)}` exited with {code}"]


def _driver() -> list[str]:
    """Ruff by `ruff.toml`, which an editor reads too, and mypy, against the oldest Python `omw` supports. **Mypy
    once for each system the driver builds on**, with `WINDOWS` a constant to it, so each system's branches are
    checked against that system's `os`."""
    env = {**os.environ, "MYPYPATH": str(ROOT / "tools")}
    found = _ran([*command_for(RUFF), "check", "--config", "tools/omw/ruff.toml", "tools/omw", "omw"])
    mypy = command_for(MYPY)
    for platform in SYSTEMS:
        windows = "true" if platform == "win32" else "false"
        found += _ran([*mypy, "--python-version", "3.11", "--platform", platform, f"--always-{windows}", "WINDOWS",
                       "--cache-dir", str(OUT / "mypy"), "-p", "omw"], env)
    return found


# The image every Linux build runs in, pinned by digest in one place: the release base's `FROM`.
RELEASE_BASE_FILE = Path("env") / "release-base.Dockerfile"


def _workflows() -> list[str]:
    """zizmor, offline, and every workflow's container the release base's image: a finding is a failure."""
    return _containers(ROOT) + _ran([*command_for(ZIZMOR), "--offline", "."])


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
