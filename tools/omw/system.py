"""What the driver asks of the system it runs on, answered in one place."""

import argparse
import contextlib
import functools
import os
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path
from typing import IO, NoReturn

ROOT = Path(__file__).resolve().parents[2]
# Everything the driver writes, in one ignored folder: the build directories, the pinned tools and the dependency
# set (`deps`), and the release archives (`dist`).
OUT = ROOT / "out"
DEPS = OUT / "deps"
# The systems the driver builds on, by `sys.platform`, each with its half of a preset's name. Any other is refused
# before any verb (`refuse_unsupported`), rather than taking Linux's SDK and presets.
SYSTEMS = {"linux": "linux", "win32": "windows"}
WINDOWS = sys.platform == "win32"
SYSTEM = SYSTEMS.get(sys.platform, "")
EXE = ".exe" if WINDOWS else ""
# Upstream's branch, whose tree at the merge base is upstream's code. Fetched into a ref of the driver's own at
# every ask (`fetch_upstream`): no remote to configure, and never stale.
UPSTREAM_URL = "https://github.com/OpenMW/openmw.git"
UPSTREAM = "refs/omw/upstream"
# Set by GitHub Actions, as by every CI service: CI checks what the desk rewrites.
CI = os.environ.get("CI", "").lower() in ("true", "1")
# The tree's own C++, which the formatting and the file names are checked over: `extern/` is vendored.
CXX_SOURCES = (":(exclude)extern/", "*.cpp", "*.hpp", "*.h")
# The CMake files the tree formats: not those vendored under `extern/`, nor the vcpkg ports copied from their
# registries, kept as written so a registry's next version compares line for line.
CMAKE_SOURCES = ("*CMakeLists.txt", "*.cmake", ":(exclude)extern/*/*", ":(exclude)vcpkg/ports/*")


class Refusal(Exception):
    """A request refused, or a step that cannot go on, with the reason said to a person."""


def build_dir(flavour: str) -> Path:
    """Where `flavour` builds, as `CMakePresets.json`'s `binaryDir` names it."""
    return OUT / flavour


class Switches(argparse.ArgumentParser):
    """A driver verb's own switches: `parse_known_args` leaves the rest for the program the verb runs. **No
    abbreviations**, or a prefix of a harness switch would be taken for the verb's; a switch spelled wrong is a
    `Refusal`, not argparse's exit."""

    def __init__(self, verb: str, description: str):
        super().__init__(prog=f"omw {verb}", description=description, allow_abbrev=False)
        self.verb = verb

    def error(self, message: str) -> NoReturn:
        raise Refusal(f"{self.verb}: {message}")


def refuse_unsupported(platform: str = sys.platform) -> None:
    """Refuses a system the driver does not build on, before it downloads or configures anything."""
    if platform not in SYSTEMS:
        raise Refusal(f"the driver builds on Linux and Windows, and this system is {platform}")


def require(tool: str, why: str) -> str:
    """The path of a tool a step needs, asked for before the step starts anything."""
    found = on_path(tool)
    if found is None:
        raise Refusal(f"{tool} is not on the PATH, and this needs it: {why}")
    return found


def resolved(command: list, env: dict[str, str] | None, cwd: str | Path | None = None) -> list[str]:
    """The command with its program found where it runs: a bare name on the PATH of `env`, a relative path from
    `cwd`. POSIX does both itself; Windows searches the driver's own PATH, which lacks what VsDevCmd added, and
    reads a relative path from the driver's directory. A string, since `Path` drops a leading `./`."""
    parts = [str(part) for part in command]
    program = parts[0]
    if not os.path.dirname(program):
        if env is not None and WINDOWS:
            found = on_path(program, env.get(environment_key("PATH"), ""))
            if found is None:
                raise Refusal(f"{program} is not on the PATH it runs under")
            parts[0] = found
    elif cwd is not None:
        # Absolute, as POSIX reads a relative program from `cwd` after it changed there.
        parts[0] = str(Path(cwd).absolute() / program)
    return parts


def on_path(program: str, path: str | None = None) -> str | None:
    """`program` from the folders `path` names alone, the driver's PATH by default, and never the working
    directory, which `shutil.which` searches first on Windows. There a name without a `PATHEXT` extension is
    tried with each, which Python 3.11's `which` skips for a name with a folder in it."""
    searched = os.environ.get("PATH", "") if path is None else path
    names = [program]
    if WINDOWS:
        extensions = [extension for extension in os.environ.get("PATHEXT", ".COM;.EXE;.BAT;.CMD").split(";")
                      if extension]
        if os.path.splitext(program)[1].upper() not in (extension.upper() for extension in extensions):
            names = [program + extension for extension in extensions]
    for folder in filter(None, searched.split(os.pathsep)):
        for name in names:
            candidate = os.path.join(folder, name)
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
    return None


def run(command: list, **options) -> subprocess.CompletedProcess:
    """A command run to its end. A failure is a `CalledProcessError`, which `main` reports."""
    return subprocess.run(resolved(command, options.get("env"), options.get("cwd")), check=True, **options)


def output(command: list, **options) -> str:
    """What a command run to its end printed, as text."""
    return subprocess.run(resolved(command, options.get("env"), options.get("cwd")), check=True, capture_output=True,
                          encoding="utf-8", errors="replace", **options).stdout


def report(findings: list[str]) -> int:
    """A check's findings, a line each, and its exit status: nought where it found nothing."""
    for line in findings:
        print(line, flush=True)
    return 1 if findings else 0


def status(code: int) -> int:
    """A child's `returncode` as the shell spells an exit status: a death by signal N is `128 + N`, where an exit
    of Python's `-N` wraps to `256 - N`."""
    return 128 - code if code < 0 else code


@contextlib.contextmanager
def temporary_folder(prefix: str) -> Iterator[Path]:
    """A folder for a run's logs and pictures, removed where an exception leaves it empty and kept otherwise,
    because the message names what is in it."""
    folder = Path(tempfile.mkdtemp(prefix=prefix))
    try:
        yield folder
    except BaseException:
        # `rmdir` removes only an empty folder, which is the whole of the rule.
        with contextlib.suppress(OSError):
            folder.rmdir()
        raise


def _take_lock(held: IO[str], wait: bool) -> bool:
    """Takes the whole-process lock on the open file `held`, waiting where `wait` says, and answers whether it
    did. Closing the file or ending the process lets it go, so a run that dies leaves nothing to clear."""
    if sys.platform == "win32":
        import msvcrt

        while True:
            try:
                held.seek(0)
                msvcrt.locking(held.fileno(), msvcrt.LK_NBLCK, 1)
                return True
            except OSError:
                if not wait:
                    return False
                time.sleep(0.5)
    else:
        import fcntl

        try:
            fcntl.flock(held, fcntl.LOCK_EX | (0 if wait else fcntl.LOCK_NB))
            return True
        except BlockingIOError:
            return False


@contextlib.contextmanager
def exclusive(lock_file: Path, what: str) -> Iterator[None]:
    """One process at a time inside: a second waits, and says what for."""
    lock_file.parent.mkdir(parents=True, exist_ok=True)
    with lock_file.open("a+") as held:
        if not _take_lock(held, wait=False):
            print(f"omw: waiting for another omw that is {what}", file=sys.stderr, flush=True)
            _take_lock(held, wait=True)
        yield


def read_text(path: Path) -> str:
    """A file as text, **UTF-8 whatever the locale**, which is cp1252 on Windows; what does not decode is
    replaced, so a log a crash cut mid-character still reads."""
    return path.read_text(encoding="utf-8", errors="replace")


def working_tree_files(*pathspecs: str, root: Path = ROOT) -> list[str]:
    """The files `pathspecs` name in the working tree, tracked or new and not ignored, from the root,
    `/`-separated. **Not the index's**, which still holds a file a move took and lacks one it made."""
    listed = output(["git", "-C", root, "ls-files", "--cached", "--others", "--exclude-standard", "--",
                     *pathspecs]).splitlines()
    return [name for name in listed if (root / name).is_file()]


def fetch_upstream(root: Path = ROOT, url: str = UPSTREAM_URL) -> None:
    """Upstream's master into `UPSTREAM`."""
    try:
        output(["git", "-C", root, "fetch", "--quiet", "--no-tags", url, f"+master:{UPSTREAM}"])
    except subprocess.CalledProcessError as error:
        raise Refusal(f"upstream's master, which tells the fork's files from upstream's, could not be fetched from "
                      f"{url}: {error.stderr.strip()}") from error


def files_upstream_lacks(root: Path = ROOT) -> frozenset[str]:
    """The working tree's files that `UPSTREAM`'s tree lacks at its merge base with `HEAD`, from the root,
    `/`-separated, the vendored `extern/` aside."""
    base = output(["git", "-C", root, "merge-base", "HEAD", UPSTREAM]).strip()
    upstream = set(output(["git", "-C", root, "ls-tree", "-r", "-z", "--name-only", base]).split("\0"))
    return frozenset(name for name in working_tree_files(":(exclude)extern/", root=root) if name not in upstream)


@functools.cache
def fork_files(root: Path = ROOT) -> frozenset[str]:
    """**The fork's own files, which the fork's checks hold**: those upstream's master lacks as it stands now.
    Derived and not listed, so a file the fork adds anywhere is its own the moment it exists."""
    fetch_upstream(root)
    return files_upstream_lacks(root)


def user_and_group() -> str:
    """This process's user and group as `uid:gid`, the form `chown` and `docker --user` take; POSIX's alone."""
    # An `if` and not an `assert`, which mypy does not read a platform through inside a function.
    if WINDOWS:
        raise AssertionError("a user and a group are POSIX's, and no step on Windows asks for them")
    return f"{os.getuid()}:{os.getgid()}"


def jobs() -> int:
    return os.cpu_count() or 1


def environment_key(name: str) -> str:
    """A variable's name as `os.environ` holds it: in capitals on Windows, which reads names without case."""
    return name.upper() if WINDOWS else name


def prepend_path(env: dict[str, str], name: str, directory: Path) -> None:
    key = environment_key(name)
    env[key] = os.pathsep.join(filter(None, [str(directory), env.get(key, "")]))


def parse_set_output(text: str) -> dict[str, str]:
    """The variables cmd's `set` printed, a `NAME=value` a line; cmd's per-drive `=C:` entries are none."""
    variables: dict[str, str] = {}
    for line in text.splitlines():
        name, equals, value = line.partition("=")
        if equals and name:
            variables[environment_key(name)] = value
    return variables


def msvc_environment() -> dict[str, str]:
    """**MSVC's environment, the way setuptools takes it**, a copy the caller may change: `VsDevCmd.bat` for the
    installation vswhere names, then `set` in the same `cmd`. Every build needs it, as Ninja does not bake `INCLUDE`
    and `LIB` in. Once a process, since the batch file takes seconds."""
    return dict(_msvc_activated())


@functools.cache
def _msvc_activated() -> dict[str, str]:
    env = dict(os.environ)
    # The documented sign that a shell has the environment already.
    if env.get(environment_key("VSCMD_VER")):
        return env

    programs = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))  # noqa: SIM112 Windows' name
    installer = programs / "Microsoft Visual Studio" / "Installer"
    vswhere = installer / "vswhere.exe"
    if not vswhere.is_file():
        raise Refusal(f"no Visual Studio installer at {installer}, so no vswhere to find a compiler with")

    studio = output([vswhere, "-latest", "-products", "*", "-requires",
                     "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"]).strip()
    if not studio:
        raise Refusal("vswhere found no Visual Studio with the C++ tools")

    batch = Path(studio) / "Common7" / "Tools" / "VsDevCmd.bat"
    # The batch file looks for vswhere on the PATH, and `-startdir=none` keeps it from moving the shell.
    searched = dict(env)
    prepend_path(searched, "PATH", installer)
    # One string, because `/s` takes the first and the last quote off it and runs the rest as typed; `/u` prints
    # UTF-16.
    line = f'cmd.exe /u /s /c "call "{batch}" -no_logo -arch=amd64 -host_arch=amd64 -startdir=none && set"'
    dumped = subprocess.run(line, check=True, capture_output=True, env=searched).stdout
    activated = parse_set_output(dumped.decode("utf-16-le", errors="replace"))

    if not activated.get("VSCMD_VER") or on_path("cl", activated.get("PATH", "")) is None:
        raise Refusal("VsDevCmd.bat ran and left no compiler on the PATH")
    return activated
