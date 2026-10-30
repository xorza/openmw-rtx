"""`omw [flavour] remote <host> <verb> [args]`: openmw-rtxtool's `verb` on another Linux machine over
SSH — a Steam Deck, or any box whose driver is the question — with what it writes brought back.

**Built where the release is built**: in the release-base container on a desk that is another system
(`package.needs_container`), so the binary asks no newer glibc than Ubuntu 24.04's, and run against
the other machine's own Vulkan loader and driver, which are what the run measures. The libraries the
binary links that the other machine has no soname of go beside it, with OSG's plugins, which no
linker names; everything it does have is its own.

**At the same path there**, `build-<flavour>` under this tree, because the harness reads its folder,
its views and its shaders at the paths the build compiled in. The machine's user owns the tree's
parent folder, made once by hand.

**Headless**: SDL's offscreen driver, since the session is SSH's and has no display, and a bench is
told `--window=false` unless the line says otherwise. A run that measures (`bench`, `noise`) holds a
SteamOS GPU clock at its top through `steamos-manager` for its frames, and puts it back after: the
Deck otherwise moves its clock with its power, and two runs at two clocks are no A/B. Not a run with
`--counters`: RADV puts the card in a profiling state of the kernel's while it reads them, which on
the Deck holds 1300 MHz, `standard` and `peak` alike, and the report's clock line says so.

**The files a run names come back**: `--json`, `--frame-times`, `--out` and `--pipeline-code` are
written there at the same absolute path and copied here after it, and what `--against` and `--keys`
name is copied there first."""

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
    osg_plugin_folder,
    prepare_container,
    used_osg_plugins,
)
from omw.system import ROOT, WINDOWS, Refusal, output, read_text, require, run, status

# What the run reads from the build: the binary, the configuration and the defaults beside it, and
# the folders of the resources, the shaders among them, and of the harness.
SHIPPED = ("openmw-rtxtool", "openmw.cfg", "defaults.bin", "gamecontrollerdb.txt", "resources", "rtxtool")

# The folder beside the binary the libraries it lacks go to, on the other machine.
LIBRARIES = "remote-lib"

# The harness's switches that name what a run writes, and what it reads.
OUTPUTS = ("json", "frame-times", "out", "pipeline-code")
INPUTS = ("against", "keys")

# A switch that names something of this machine's that the other cannot reach: perf's fifo.
LOCAL = ("perf-control",)

# The verbs whose figures are quoted, which run under a held clock.
MEASURED = ("bench", "noise")

SSH = ("-o", "BatchMode=yes")

# SDL's driver with no display and Vulkan all the same: `dummy` has none.
HEADLESS = "SDL_VIDEO_DRIVER=offscreen"

# Where `steamos-manager` keeps the GPU's performance level and its manual clock, which it writes to
# the card again and again: a level written to sysfs past it did not hold.
GPU_LEVEL = ("com.steampowered.SteamOSManager1 /com/steampowered/SteamOSManager1 "
             "com.steampowered.SteamOSManager1.GpuPerformanceLevel1")


def sonames(ldconfig: str) -> set[str]:
    """The sonames an `ldconfig -p` listing names, its header and its arrows aside."""
    return {line.split()[0] for line in ldconfig.splitlines()[1:] if line.startswith(("\t", " ")) and line.split()}


def linked(ldd: str) -> dict[str, str]:
    """`soname: path` for each library an `ldd` listing resolves, once each. The loader, the vDSO and
    what did not resolve name no file to copy."""
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
    """A harness line for the other machine: its paths made absolute, with the files it writes and
    the ones it reads."""
    args: list[str]
    outputs: list[Path] = field(default_factory=list)
    inputs: list[Path] = field(default_factory=list)


def remote_line(args: list[str], verb: str, here: Path) -> RemoteLine:
    """`args` with every path `OUTPUTS` and `INPUTS` name made absolute against `here`, in either of
    the forms the harness takes — `--name=value` and `--name value` — and a bench told
    `--window=false` where it says nothing of a window."""
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
    """What runs on the other machine, as bash: in `folder`, under its libraries, the harness's `verb`.
    **Not exec'd**, so the clock a measured run held is put back however the run ends."""
    libraries = shlex.quote(str(folder / LIBRARIES))
    lines = [
        "set -u",
        f"cd {shlex.quote(str(folder))}",
        f"export LD_LIBRARY_PATH={libraries} OSG_LIBRARY_PATH={libraries} OPENMW_CRASH_DIALOG=0 {HEADLESS}",
        "trap 'exit 130' INT TERM",
    ]
    if measured:
        lines += [
            f'gpu() {{ busctl --user "$1" {GPU_LEVEL} "${{@:2}}"; }}',
            "if level=$(gpu get-property GpuPerformanceLevel 2>/dev/null); then",
            "  level=${level#s }; level=${level//\\\"/}",
            "  clock=$(gpu get-property ManualGpuClock); clock=${clock#u }",
            "  top=$(gpu get-property ManualGpuClockMax); top=${top#u }",
            "  trap 'gpu set-property ManualGpuClock u \"$clock\"; gpu set-property GpuPerformanceLevel s \"$level\"' EXIT",
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
    if not _answers(ssh, host, f"test -w {shlex.quote(str(folder.parent))}"):
        raise Refusal(f"{host} cannot write {folder.parent}, where the build goes: make it once there with "
                      f"`sudo mkdir -p {folder.parent} && sudo chown $USER: {folder.parent}`")

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
        run([rsync, "-a", f"{path}/" if path.is_dir() else str(path), f"{host}:{path}" + ("/" if path.is_dir() else "")])
    for path in line.outputs:
        run([ssh, *SSH, host, f"mkdir -p {shlex.quote(str(path.parent))}"])

    script = remote_script(folder, verb, line.args, verb in MEASURED)
    encoded = base64.b64encode(script.encode()).decode()
    # A terminal where this one has one, so Ctrl-C stops the run there and not only the connection.
    terminal = ["-tt"] if sys.stdin.isatty() else []
    ran = subprocess.run([ssh, *SSH, *terminal, host, f"echo {encoded} | base64 -d | bash"], check=False)

    for path in line.outputs:
        fetched = subprocess.run([rsync, "-a", f"{host}:{path}", str(path.parent) + "/"], check=False,
                                 stderr=subprocess.DEVNULL)
        print(f"remote: {path}" + ("" if fetched.returncode == 0 else " was not written there"), flush=True)
    return status(ran.returncode)


def _answers(ssh: str, host: str, command: str) -> bool:
    return subprocess.run([ssh, *SSH, host, command], check=False).returncode == 0


@dataclass(frozen=True)
class Staged:
    """The harness built where a release is built: its folder as this desk sees it, the container it
    was built in or none for the desk's own build, OSG's plugins and the folder the build found them
    in, and what `ldd` says the harness and the plugins link."""
    folder: Path
    container: Container | None
    plugin_folder: Path
    plugins: list[str]
    ldd: str

    def inside(self, command: list[str]) -> list[str]:
        """`command` where the build was made, since the paths it names are that system's."""
        return self.container.command(command) if self.container is not None else command

    @property
    def libraries(self) -> Path:
        """Where the libraries the other machine lacks are gathered, beside the build."""
        return self.folder.parent / f"{self.folder.name}-{LIBRARIES}"


def _build(build: Build) -> Staged:
    container = prepare_container(build.flavour) if needs_container() else None
    if container is None:
        build.build_harness()
        reading = build
    else:
        run(container.command(["python3", "omw", build.flavour, "build", "openmw-rtxtool"]))
        reading = Build(build.flavour)
        reading.dir = CONTAINER_DIR / build.flavour

    plugin_folder = osg_plugin_folder(reading)
    plugins = used_osg_plugins(read_text(ROOT / "CMakeLists.txt"))
    # The binary at the path the container sees its build at, which is the desk's own build's.
    listing = ["ldd", str(build.dir / "openmw-rtxtool"), *(str(plugin_folder / f"{plugin}.so") for plugin in plugins)]
    ldd = output(container.command(listing) if container is not None else listing)
    return Staged(reading.dir, container, plugin_folder, plugins, ldd)


def _stage_libraries(staged: Staged, missing: dict[str, str]) -> None:
    """Copies each of `missing` under its soname, and OSG's plugins under their folder's name, into
    `staged.libraries`, emptied first."""
    target = staged.libraries
    plugins = target / staged.plugin_folder.name
    copies = [f"rm -rf {shlex.quote(str(target))}", f"mkdir -p {shlex.quote(str(plugins))}"]
    copies += [f"cp -L {shlex.quote(path)} {shlex.quote(str(target / name))}" for name, path in missing.items()]
    copies += [f"cp -L {shlex.quote(str(staged.plugin_folder / f'{plugin}.so'))} {shlex.quote(str(plugins))}/"
               for plugin in staged.plugins]
    run(staged.inside(["bash", "-c", " && ".join(copies)]))
