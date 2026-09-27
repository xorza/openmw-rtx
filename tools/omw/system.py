"""What the driver asks of the system it runs on, answered in one place."""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEPS = ROOT / "deps"
WINDOWS = os.name == "nt"
# The system's half of a preset's name.
SYSTEM = "windows" if WINDOWS else "linux"
EXE = ".exe" if WINDOWS else ""


class Refusal(Exception):
    """A request refused, or a step that cannot go on, with the reason said to a person."""


def require(tool: str, why: str) -> str:
    """The path of a tool a step needs, asked for before the step starts anything."""
    found = shutil.which(tool)
    if found is None:
        raise Refusal(f"{tool} is not on the PATH, and this needs it: {why}")
    return found


def resolved(command: list, env: dict[str, str] | None) -> list[str]:
    """The command with its program found on the PATH of the environment it runs under. POSIX
    searches that PATH itself; Windows searches the driver's own, which lacks what VsDevCmd added."""
    parts = [str(part) for part in command]
    if env is not None and WINDOWS and not Path(parts[0]).parent.parts:
        parts[0] = shutil.which(parts[0], path=env.get(environment_key("PATH"))) or parts[0]
    return parts


def run(command: list, **options) -> subprocess.CompletedProcess:
    """A command run to its end. A failure is a `CalledProcessError`, which `main` reports."""
    return subprocess.run(resolved(command, options.get("env")), check=True, **options)


def output(command: list, **options) -> str:
    return subprocess.run(
        resolved(command, options.get("env")), check=True, capture_output=True, text=True, **options
    ).stdout


def jobs() -> int:
    return os.cpu_count() or 1


def environment_key(name: str) -> str:
    """A variable's name as a dictionary of the environment holds it: Windows reads names without
    case, and `os.environ` there holds each in capitals."""
    return name.upper() if WINDOWS else name


def prepend_path(env: dict[str, str], name: str, directory: Path) -> None:
    key = environment_key(name)
    env[key] = os.pathsep.join(filter(None, [str(directory), env.get(key, "")]))


def parse_set_output(text: str) -> dict[str, str]:
    """The variables cmd's `set` printed, a `NAME=value` a line. cmd's own per-drive entries, `=C:`,
    are not variables."""
    variables: dict[str, str] = {}
    for line in text.splitlines():
        name, equals, value = line.partition("=")
        if equals and name:
            variables[environment_key(name)] = value
    return variables


def msvc_environment(env: dict[str, str]) -> dict[str, str]:
    """**MSVC's environment, the way setuptools takes it.** `cl` finds its headers and libraries
    through `INCLUDE` and `LIB`, which Ninja does not bake into the build, so every build needs them
    and not only the configure. `VsDevCmd.bat` sets them for the installation vswhere names, and a
    `set` in the same `cmd` prints the whole environment it left, as UTF-16 under `/u`. `VSCMD_VER`
    is the documented sign that a shell has them already. vswhere's directory goes on the PATH,
    because the batch file looks for it there, and `-startdir=none` keeps it from moving the shell.
    """
    if env.get(environment_key("VSCMD_VER")):
        return env

    programs = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    installer = programs / "Microsoft Visual Studio" / "Installer"
    vswhere = installer / "vswhere.exe"
    if not vswhere.is_file():
        raise Refusal(f"no Visual Studio installer at {installer}, so no vswhere to find a compiler with")

    studio = output([vswhere, "-latest", "-products", "*", "-requires",
                     "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"]).strip()
    if not studio:
        raise Refusal("vswhere found no Visual Studio with the C++ tools")

    batch = Path(studio) / "Common7" / "Tools" / "VsDevCmd.bat"
    searched = dict(env)
    prepend_path(searched, "PATH", installer)
    # One string, because `/s` takes the first and the last quote off it and runs the rest as typed.
    line = f'cmd.exe /u /s /c "call "{batch}" -no_logo -arch=amd64 -host_arch=amd64 -startdir=none && set"'
    dumped = subprocess.run(line, check=True, capture_output=True, env=searched).stdout
    activated = parse_set_output(dumped.decode("utf-16-le", errors="replace"))

    if not activated.get("VSCMD_VER") or shutil.which("cl", path=activated.get("PATH")) is None:
        raise Refusal("VsDevCmd.bat ran and left no compiler on the PATH")
    return activated


def documents_folder() -> Path:
    """The Documents folder the system names, which is not `~/Documents` on a box where OneDrive
    has moved it."""
    assert sys.platform == "win32"
    import ctypes

    buffer = ctypes.create_unicode_buffer(260)
    personal = 5  # CSIDL_PERSONAL
    if ctypes.windll.shell32.SHGetFolderPathW(None, personal, None, 0, buffer) != 0:
        raise Refusal("the system names no Documents folder")
    return Path(buffer.value)


def user_config_dir() -> Path:
    """Where the game reads its own openmw.cfg and settings.cfg, answered the way
    `Files::LinuxPath` and `Files::WindowsPath` answer it."""
    if WINDOWS:
        return documents_folder() / "My Games" / "OpenMW"
    return Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "openmw"


def user_data_dir() -> Path:
    """Where the game keeps its saves: one directory with the configuration on Windows, two on Linux."""
    if WINDOWS:
        return user_config_dir()
    return Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share") / "openmw"
