"""`omw [flavour] remote <host> <verb> [args]`: openmw-rtxtool's `verb` on another Linux machine over SSH, with what
it writes brought back. **Built where the release is**, so it asks no newer glibc than AlmaLinux 8's, and run there
against the machine's own loader and driver, with the libraries it lacks beside it, **at the same path**, which the
harness reads its folders at. Headless (`SESSION`); a measured run holds a SteamOS GPU clock at its top."""

import base64
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from omw import noise
from omw.build import Build
from omw.package import (
    CONTAINER_DIR,
    Container,
    needs_container,
    prepare_container,
)
from omw.system import WINDOWS, Refusal, output, require, run, status

# What the run reads from the build: the binary, the configuration and the defaults beside it, and
# the folders of the resources, the shaders among them, and of the harness.
SHIPPED = ("openmw-rtxtool", "openmw.cfg", "defaults.bin", "gamecontrollerdb.txt", "resources", "rtxtool")

# The folder beside the binary the libraries it lacks go to, on the other machine.
LIBRARIES = "remote-lib"

# The harness's switches that name what a run writes, and what it reads.
OUTPUTS = ("json", "frame-times", "out", "pipeline-code")
INPUTS = ("against", "keys")

# A switch that names what the other machine cannot reach: perf's fifo.
LOCAL = ("perf-control",)

# The verbs whose figures are quoted, which run under a held clock.
MEASURED = ("bench", "noise")

SSH = ("-o", "BatchMode=yes")

# **The machine's own Wayland session where it has one, and SDL's offscreen driver where it has none**, as `dummy`
# has no Vulkan: the session first, where a hidden window presents as a played game does.
SESSION = ('runtime=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}; '
           'if [ -S "$runtime/wayland-0" ]; then '
           'export XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY=wayland-0 SDL_VIDEO_DRIVER=wayland; '
           'else export SDL_VIDEO_DRIVER=offscreen; fi')

# Where `steamos-manager` keeps the GPU's performance level and its manual clock, which it writes to the card again
# and again, so a level written to sysfs past it does not hold.
GPU_LEVEL = ("com.steampowered.SteamOSManager1 /com/steampowered/SteamOSManager1 "
             "com.steampowered.SteamOSManager1.GpuPerformanceLevel1")


def sonames(ldconfig: str) -> set[str]:
    """The sonames an `ldconfig -p` listing names, its header and its arrows aside."""
    return {line.split()[0] for line in ldconfig.splitlines()[1:] if line.startswith(("\t", " ")) and line.split()}


def linked(ldd: str) -> dict[str, str]:
    """`soname: path` for each library an `ldd` listing resolves, once each; the loader, the vDSO and what did not
    resolve name no file."""
    found: dict[str, str] = {}
    for line in ldd.splitlines():
        name, arrow, rest = line.strip().partition(" => ")
        path = rest.split(" (")[0].strip()
        if arrow and path.startswith("/"):
            found.setdefault(name, path)
    return found


def lacking(libraries: dict[str, str], offered: set[str]) -> dict[str, str]:
    """Of `libraries`, the ones `offered` names no soname of, in name order."""
    return {name: libraries[name] for name in sorted(libraries) if name not in offered}


@dataclass
class RemoteLine:
    """A harness line for the other machine: its paths made absolute, with the files it writes and reads."""
    args: list[str]
    outputs: list[Path] = field(default_factory=list)
    inputs: list[Path] = field(default_factory=list)


def remote_line(args: list[str], verb: str, here: Path) -> RemoteLine:
    """`args` with every path `OUTPUTS` and `INPUTS` name made absolute against `here`, as `--name=value` or `--name
    value`, and a bench told `--window=false` where it says nothing of a window."""
    line = RemoteLine(args=[])
    at = 0
    while at < len(args):
        word = args[at]
        name, equals, value = word.partition("=")
        switch = name.removeprefix("--")
        if name.startswith("--") and switch in LOCAL:
            raise Refusal(f"remote: {name} names what is this machine's, which the run there cannot reach")
        if name.startswith("--") and switch in (*OUTPUTS, *INPUTS):
            if not equals:
                if at + 1 >= len(args):
                    raise Refusal(f"remote: {name} names no path")
                at += 1
                value = args[at]
            # Absolute and not resolved: a link of this machine's is no path of the other's.
            path = Path(os.path.normpath(here / value)) if value else Path()
            if value:
                (line.outputs if switch in OUTPUTS else line.inputs).append(path)
            line.args.append(f"{name}={path}" if value else f"{name}=")
        else:
            line.args.append(word)
        at += 1

    if verb == "bench" and not any(word == "--window" or word.startswith("--window=") for word in args):
        line.args.append("--window=false")
    return line


def remote_script(folder: Path, verb: str, args: list[str], measured: bool) -> str:
    """What runs on the other machine, as bash: the harness's `verb` in `folder`, under its libraries. **Not
    exec'd**, so the clock a measured run held is put back however the run ends."""
    libraries = shlex.quote(str(folder / LIBRARIES))
    lines = [
        "set -u",
        f"cd {shlex.quote(str(folder))}",
        f"export LD_LIBRARY_PATH={libraries} OPENMW_CRASH_DIALOG=0",
        SESSION,
        "trap 'exit 130' INT TERM",
    ]
    if measured:
        lines += [
            f'gpu() {{ busctl --user "$1" {GPU_LEVEL} "${{@:2}}"; }}',
            "if level=$(gpu get-property GpuPerformanceLevel 2>/dev/null); then",
            "  level=${level#s }; level=${level//\\\"/}",
            "  clock=$(gpu get-property ManualGpuClock); clock=${clock#u }",
            "  top=$(gpu get-property ManualGpuClockMax); top=${top#u }",
            # The clock a level other than manual neither holds nor takes: steamos-manager refuses it.
            ("  trap 'gpu set-property GpuPerformanceLevel s \"$level\"; [ \"$level\" = manual ] "
             "&& gpu set-property ManualGpuClock u \"$clock\"' EXIT"),
            "  gpu set-property GpuPerformanceLevel s manual && gpu set-property ManualGpuClock u \"$top\"",
            "  echo \"remote: the GPU clock held at $top MHz through steamos-manager, $level before and after\"",
            "fi",
        ]
    lines.append(shlex.join(["./openmw-rtxtool", verb, *args]))
    return "\n".join(lines) + "\n"


def remote(build: Build, args: list[str], harness_verbs: tuple[str, ...]) -> int:
    if WINDOWS:
        raise Refusal("remote is Linux's: the build it copies is a Linux one")
    if len(args) < 2:
        raise Refusal("remote runs a harness verb on a host: `omw [flavour] remote <user@host> <verb> [args]`")
    host, verb, rest = args[0], args[1], args[2:]
    if host.startswith("-"):
        raise Refusal(f"remote: {host!r} is no host, and ssh would read it as a switch")
    if verb not in harness_verbs:
        raise Refusal(f"remote runs the harness's verbs, and {verb!r} is none: {', '.join(harness_verbs)}")
    if verb == "view":
        raise Refusal("remote has no display to open a window on")
    if verb == "noise" and noise.wants_ab(rest):
        raise Refusal("remote runs the harness's own noise, and an A/B is the driver's: run each side there")

    # Read before anything is built or copied, so a line that cannot run there is refused at once.
    line = remote_line(rest, verb, Path.cwd())
    if absent := [str(path) for path in line.inputs if not path.exists()]:
        raise Refusal(f"remote: nothing here to copy there: {', '.join(absent)}")
    ssh = require("ssh", "it is what reaches the other machine: ssh")
    rsync = require("rsync", "it is what copies the build there and the files back: rsync")

    folder = build.dir
    probed = subprocess.run([ssh, *SSH, host, f"test -w {shlex.quote(str(folder.parent))}"], check=False)
    if refused := build_refusal(host, folder.parent, probed.returncode):
        raise Refusal(refused)

    staged = _build(build)
    offered = sonames(output([ssh, *SSH, host, "ldconfig -p"]))
    missing = lacking(linked(staged.ldd), offered)
    _stage_libraries(staged, missing)
    print(f"remote: {len(missing)} libraries {host} lacks go beside the binary", flush=True)

    run([ssh, *SSH, host, f"mkdir -p {shlex.quote(str(folder / LIBRARIES))}"])
    run([rsync, "-a", *(staged.folder / name for name in SHIPPED), f"{host}:{folder}/"])
    run([rsync, "-a", "--delete", f"{staged.libraries}/", f"{host}:{folder / LIBRARIES}/"])

    for path in line.inputs:
        run([ssh, *SSH, host, f"mkdir -p {shlex.quote(str(path.parent))}"])
        trailing = "/" if path.is_dir() else ""
        run([rsync, "-a", f"{path}{trailing}", f"{host}:{path}{trailing}"])
    for path in line.outputs:
        run([ssh, *SSH, host, f"mkdir -p {shlex.quote(str(path.parent))}"])

    # Not a `--counters` run: RADV holds the kernel's profiling clock under it whatever is set.
    held = verb in MEASURED and not any(word == "--counters" or word.startswith("--counters=") for word in rest)
    script = remote_script(folder, verb, line.args, held)
    encoded = base64.b64encode(script.encode()).decode()
    # A terminal where this one has one, so Ctrl-C stops the run there and not only the connection.
    terminal = ["-tt"] if sys.stdin.isatty() else []
    ran = subprocess.run([ssh, *SSH, *terminal, host, f"echo {encoded} | base64 -d | bash"], check=False)

    for path in line.outputs:
        fetched = subprocess.run([rsync, "-a", f"{host}:{path}", str(path.parent) + "/"], check=False,
                                 stderr=subprocess.DEVNULL)
        print(f"remote: {path}{fetch_note(fetched.returncode)}", flush=True)
    return status(ran.returncode)


def build_refusal(host: str, parent: Path, status: int) -> str | None:
    """Why the build cannot go under `parent` on `host`, read off ssh's status from `test -w` there, or nothing where
    it can. **255 is ssh's own failure**, a host it cannot reach among them; `test` answers 0 or 1."""
    if status == 0:
        return None
    if status == 255:
        return f"remote: ssh reached no shell on {host}, as it says above"
    return (f"{host} cannot write {parent}, where the build goes: make it once there with "
            f"`sudo mkdir -p {parent} && sudo chown $USER: {parent}`")


def fetch_note(status: int) -> str:
    """What a path brought back is said with, by rsync's status: nothing where it came, and where it did not, whether
    the run wrote none — **23**, rsync's partial transfer, a missing source — or the copy itself failed."""
    if status == 0:
        return ""
    if status == 23:
        return " was not written there"
    return f" could not be brought back: rsync exited {status}"


@dataclass(frozen=True)
class Staged:
    """The harness built where a release is: its folder as this desk sees it, the container it was built in or none,
    and what `ldd` says it links."""
    folder: Path
    container: Container | None
    ldd: str

    def inside(self, command: list[str]) -> list[str]:
        """`command` where the build was made, since the paths it names are that system's."""
        return self.container.command(command) if self.container is not None else command

    @property
    def libraries(self) -> Path:
        """Where the libraries the other machine lacks are gathered, beside the build."""
        return self.folder.parent / f"{self.folder.name}-{LIBRARIES}"


def _build(build: Build) -> Staged:
    if not needs_container():
        build.build_harness()
        return Staged(build.dir, None, output(["ldd", str(build.binary("openmw-rtxtool"))]))
    container = prepare_container(build.flavour)
    run(container.command(["python3", "omw", build.flavour, "build", "openmw-rtxtool"]))
    # The binary at the path the container sees its build at, which is the desk's own build's.
    ldd = output(container.command(["ldd", str(build.binary("openmw-rtxtool"))]))
    return Staged(CONTAINER_DIR / build.flavour, container, ldd)


def _stage_libraries(staged: Staged, missing: dict[str, str]) -> None:
    """Copies each of `missing` under its soname into `staged.libraries`, emptied first."""
    target = staged.libraries
    copies = [f"rm -rf {shlex.quote(str(target))}", f"mkdir -p {shlex.quote(str(target))}"]
    copies += [f"cp -L {shlex.quote(path)} {shlex.quote(str(target / name))}" for name, path in missing.items()]
    run(staged.inside(["bash", "-c", " && ".join(copies)]))
