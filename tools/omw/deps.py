"""What a build needs and no system package brings, fetched into deps/ at the versions `pins.py` names. Each tool
stands in a folder named after its pins (`pinned_folder`), so a bump fetches anew and an unchanged pin fetches
nothing."""

import hashlib
import io
import shutil
import subprocess
import sys
import tarfile
import zipfile
from collections.abc import Callable
from pathlib import Path

from omw import fetch, pins
from omw.system import (
    DEPS,
    EXE,
    ROOT,
    SYSTEM,
    WINDOWS,
    Refusal,
    environment_key,
    prepend_path,
    working_tree_files,
)


def pinned_folder(name: str, *pinned: pins.Pin) -> Path:
    """**Where tools fetched by `pinned` stand, named after their digests**: a changed pin is a folder not there
    yet, so a desk or a CI cache holding the old tool fetches the new one rather than keep the old."""
    digest = hashlib.sha256("".join(pin.sha256 for pin in pinned).encode()).hexdigest()[:16]
    return DEPS / f"{name}-{digest}"


def _fetched_once(folder: Path, fill: Callable[[Path], None], within: str = "") -> Path:
    """`folder`, filled by `fill` the first time it is asked for, `within` it taking the name (`fetch.build_beside`).
    **Downloads land inside the partial folder** `fill` is handed, so a run cut off leaves nothing a prune must find."""
    if not folder.is_dir():
        fetch.build_beside(folder, fill, within)
    return folder


def _download_into(partial: Path, pin: pins.Pin) -> Path:
    archive = partial / Path(pin.url).name
    partial.mkdir(parents=True, exist_ok=True)
    fetch.download_pin(pin, archive)
    return archive


def clang_format_dir() -> Path:
    return pinned_folder("clang-format", pins.CLANG_FORMAT[SYSTEM])


def clang_format() -> Path:
    """**The clang-format every desk and CI formats with**, the one program out of the wheel `pins.py` names for
    this system, read and never installed: one layout on every system."""
    program = f"clang-format{EXE}"

    def fill(partial: Path) -> None:
        wheel = _download_into(partial, pins.CLANG_FORMAT[SYSTEM])
        fetch.extract_member(wheel, program, partial)
        wheel.unlink()

    return _fetched_once(clang_format_dir(), fill) / program


def gersemi_pin() -> pins.Pin:
    """The gersemi wheel for this system and the CPython running the driver, whose ABI its Rust is built for."""
    tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
    pin = pins.GERSEMI.get((SYSTEM, tag))
    if pin is None:
        raise Refusal(f"pins.py names no gersemi wheel for Python {sys.version_info.major}.{sys.version_info.minor} "
                      f"on {SYSTEM}: CPython 3.10 to 3.14")
    return pin


def gersemi_dir() -> Path:
    """Where this interpreter's gersemi stands: another Python's is another folder, as its wheel is."""
    return pinned_folder("gersemi", gersemi_pin(), pins.PYYAML)


def gersemi() -> list[str]:
    """**The gersemi every desk and CI formats the CMake files with**, as the command that runs it: its wheel
    unpacked beside PyYAML's pure-Python package, run by this interpreter, isolated, with that folder alone on its
    path. Read and never installed."""

    def fill(partial: Path) -> None:
        wheel = _download_into(partial, gersemi_pin())
        with zipfile.ZipFile(wheel) as zipped:
            zipped.extractall(partial)
        wheel.unlink()
        source = _download_into(partial, pins.PYYAML)
        with tarfile.open(source) as tarred:
            members = [member for member in tarred.getmembers() if "/lib/yaml/" in member.name]
            tarred.extractall(partial / "pyyaml", members=members, filter="data")
        source.unlink()
        next((partial / "pyyaml").glob("*/lib/yaml")).rename(partial / "yaml")
        shutil.rmtree(partial / "pyyaml")

    folder = _fetched_once(gersemi_dir(), fill)
    return [sys.executable, "-I", "-c",
            "import sys; sys.path.insert(0, sys.argv.pop(1)); from gersemi.__main__ import main; sys.exit(main())",
            str(folder)]


# What vcpkg keeps between runs (`omw deps-set`): built packages, keyed by everything that built each, so a set
# rebuilds only the ports whose inputs moved; the sources it fetched; its scratch; and the packed sets.
VCPKG_BINARY_CACHE = DEPS / "vcpkg-binary-cache"
VCPKG_DOWNLOADS = DEPS / "vcpkg-downloads"
VCPKG_WORK = DEPS / "vcpkg-work"
VCPKG_SETS = DEPS / "sets"

# Each system's triplet, `vcpkg/triplets/<triplet>.cmake`.
TRIPLETS = {"linux": "x64-linux-openmw", "windows": "x64-windows-openmw"}
TRIPLET = TRIPLETS[SYSTEM]

# What a set is built from, by path from the root: the manifest and its configuration, the overlay ports and
# triplets, the image Linux builds in, and the code that prunes and packs vcpkg's export. With the vcpkg pins, the
# set's digest.
SET_INPUTS = ("vcpkg.json", "vcpkg-configuration.json", "vcpkg/*", "env/release-base.Dockerfile",
              "env/install_almalinux_deps.sh", "tools/omw/depsset.py")


def set_digest(root: Path = ROOT) -> str:
    """**The set's name: a digest of everything it is built from**, so a change to any input names another set,
    and the one published for the old inputs is never taken for it."""
    digest = hashlib.sha256()
    for pin in (pins.VCPKG_REGISTRY, *pins.VCPKG_TOOL.values()):
        digest.update(pin.sha256.encode())
    for name in sorted(working_tree_files(*SET_INPUTS, root=root)):
        digest.update(name.encode() + b"\0")
        digest.update((root / name).read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()[:16]


# **The set a build reads stands at one path whatever its digest** (`held_set_dir`), so a compile cache keeps
# every object whose headers a new set left as they were. The digest it holds is in this file beside its export;
# the sets it does not hold wait in `vcpkg_set_dir`, each under its digest.
SET_HELD = "digest"


def held_set_dir() -> Path:
    return DEPS / "vcpkg-set"


def vcpkg_set_dir(digest: str) -> Path:
    return DEPS / f"vcpkg-set-{digest}"


def held_set() -> str | None:
    """The digest of the set `held_set_dir` holds, or none: there is none, or a switch was cut off mid-way."""
    held = held_set_dir() / SET_HELD
    return held.read_text(encoding="utf-8").strip() if held.is_file() else None


def vcpkg_set() -> Path:
    """**The libraries this tree builds against**, vcpkg's export for this system's triplet: the set `omw deps-set`
    built here for the inputs as they stand, or else the one `pins.py` names, fetched. Refused where the inputs
    moved since the pinned set and this desk built none for them."""
    digest = set_digest()
    if held_set() != digest:
        waiting = vcpkg_set_dir(digest)
        if not waiting.is_dir():
            if digest != pins.VCPKG_SET.digest:
                raise Refusal(f"vcpkg.json or its overlays moved since the set pins.py names "
                              f"(deps-{pins.VCPKG_SET.digest}): `omw deps-set` builds deps-{digest} here, and the "
                              "Dependency sets workflow publishes it for pins.py")
            fetch.build_beside(waiting, _fill_set)
        _hold(waiting, digest)
    return held_set_dir() / TRIPLET


def _hold(waiting: Path, digest: str) -> None:
    """`waiting`, the set of `digest`, put at `held_set_dir`, and the set held before put back under its own
    digest: a rename each way and the digest written last, so a switch cut off leaves no set taken for another."""
    held = held_set()
    if held is not None:
        held_set_dir().rename(vcpkg_set_dir(held))
    else:
        shutil.rmtree(held_set_dir(), ignore_errors=True)
    waiting.rename(held_set_dir())
    (held_set_dir() / SET_HELD).write_text(digest + "\n", encoding="utf-8")


def place_set(export: Path, digest: str) -> None:
    """**A set this desk built, as the one of `digest`**: `export`, a triplet's folder of vcpkg's export, waits
    under its digest, and the held set gives way where it held the same digest."""
    waiting = vcpkg_set_dir(digest)
    shutil.rmtree(waiting, ignore_errors=True)
    waiting.mkdir(parents=True)
    export.replace(waiting / TRIPLET)
    if held_set() == digest:
        shutil.rmtree(held_set_dir())


def _fill_set(partial: Path) -> None:
    parts = [_download_into(partial, pin) for pin in pins.VCPKG_SET.parts(SYSTEM, TRIPLET)]
    with _Joined(parts) as joined, tarfile.open(fileobj=joined, mode="r|gz") as tarred:
        tarred.extractall(partial, filter="data")
    for part in parts:
        part.unlink()


class _Joined(io.RawIOBase):
    """The files `paths` read as one stream, in order: the parts a set is published in."""

    def __init__(self, paths: list[Path]):
        super().__init__()
        self.paths = list(paths)
        self.file: io.BufferedReader | None = None

    def readable(self) -> bool:
        return True

    def readinto(self, buffer) -> int:  # type: ignore[override]
        while True:
            if self.file is None:
                if not self.paths:
                    return 0
                self.file = self.paths.pop(0).open("rb")
            read = self.file.readinto(buffer)
            if read:
                return read
            self.file.close()
            self.file = None

    def close(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None
        super().close()


def vcpkg_root_dir() -> Path:
    return pinned_folder("vcpkg", pins.VCPKG_REGISTRY, pins.VCPKG_TOOL[SYSTEM])


def vcpkg_root() -> Path:
    """**vcpkg as `pins.py` names it**: its registry at the pinned commit with the pinned tool at its root, where
    the registry's bootstrap would put it. Without the Git history, as the tree pins versions by the commit and
    overlays, never by vcpkg's version database."""

    def fill(partial: Path) -> None:
        archive = _download_into(partial, pins.VCPKG_REGISTRY)
        fetch.unpack_stripped(archive, partial)
        archive.unlink()
        tool = partial / f"vcpkg{EXE}"
        fetch.download_pin(pins.VCPKG_TOOL[SYSTEM], tool)
        if not WINDOWS:
            tool.chmod(0o755)

    return _fetched_once(vcpkg_root_dir(), fill)


def cmake_dir() -> Path:
    return pinned_folder("cmake", pins.CMAKE[SYSTEM])


def cmake() -> Path:
    """**The CMake every build configures, builds, tests and installs with**, Kitware's own of the pinned version:
    one CMake on every desk, on CI and in the release container, whose own are older. Its `bin` goes first on the
    PATH of every command (`cmake_environment`)."""
    pin = pins.CMAKE[SYSTEM]
    # Kitware's archive holds one folder named after itself, which takes the pinned name.
    inner = Path(pin.url).name.removesuffix(".zip").removesuffix(".tar.gz")

    def fill(partial: Path) -> None:
        archive = _download_into(partial, pin)
        if WINDOWS:
            with zipfile.ZipFile(archive) as opened:
                opened.extractall(partial)
        else:
            with tarfile.open(archive) as opened:
                opened.extractall(partial, filter="data")

    return _fetched_once(cmake_dir(), fill, within=inner)


def ninja_dir() -> Path:
    return pinned_folder("ninja", pins.NINJA[SYSTEM])


def ninja() -> Path:
    """**The pinned Ninja**, the one every build runs, where a system's is as old as AlmaLinux 8's 1.8.2."""

    def fill(partial: Path) -> None:
        archive = _download_into(partial, pins.NINJA[SYSTEM])
        fetch.extract_member(archive, f"ninja{EXE}", partial)
        archive.unlink()

    return _fetched_once(ninja_dir(), fill)


def compiler_environment(env: dict[str, str]) -> None:
    """**`CC` and `CXX` the pinned Clang on Linux**: one compiler for the set and the tree on every desk, on CI and
    in the release container. Windows' is the Visual Studio its environment names."""
    if WINDOWS:
        return
    clang = llvm() / "bin"
    env["CC"] = str(clang / "clang")
    env["CXX"] = str(clang / "clang++")


def cmake_environment(env: dict[str, str]) -> None:
    """The pinned CMake first on the PATH, so the bare `cmake` and `ctest` are its own, and the pinned Ninja named
    in `OMW_NINJA`, which the presets hand CMake, so a directory configured with another is configured again."""
    env[environment_key("OMW_NINJA")] = str(ninja() / f"ninja{EXE}")
    prepend_path(env, "PATH", cmake() / "bin")


def llvm_dir() -> Path:
    return pinned_folder("llvm", pins.LLVM)


def llvm() -> Path:
    """**The pinned LLVM**: the compiler and the linker of every Linux build, the set's among them."""

    def fill(partial: Path) -> None:
        archive = _download_into(partial, pins.LLVM)
        fetch.unpack_stripped(archive, partial)
        archive.unlink()

    return _fetched_once(llvm_dir(), fill)


def appimage_tools_dir() -> Path:
    return pinned_folder("appimage", *pins.APPIMAGE_TOOLS.values())


def appimage_tools() -> Path:
    """linuxdeploy, its AppImage plugin and the AppImage runtime in one folder, where linuxdeploy looks for its
    plugins."""

    def fill(partial: Path) -> None:
        for name, pin in pins.APPIMAGE_TOOLS.items():
            fetch.download_pin(pin, partial / name)
            (partial / name).chmod(0o755)

    return _fetched_once(appimage_tools_dir(), fill)


def crash_tool_dir(name: str) -> Path:
    return pinned_folder(name, pins.CRASH_TOOLS[(name, SYSTEM)])


def crash_tool(name: str) -> Path:
    """One of Breakpad's two tools, which the player never has, at the version `pins.py` names."""
    program = f"{name}{EXE}"

    def fill(partial: Path) -> None:
        archive = _download_into(partial, pins.CRASH_TOOLS[(name, SYSTEM)])
        fetch.extract_member(archive, program, partial)
        archive.unlink()

    return _fetched_once(crash_tool_dir(name), fill) / program


# The shader tools a build and the driver run, each in the set's tools folder of the port that installs it.
SHADER_TOOLS = {"glslc": "shaderc", "spirv-val": "spirv-tools", "spirv-opt": "spirv-tools",
                "spirv-dis": "spirv-tools"}


def vulkan_loader_dir() -> Path:
    return pinned_folder("vulkan-loader", pins.VULKAN_LOADER_WINDOWS)


def vulkan_loader() -> Path:
    """**The Vulkan loader Windows' tests start against**, LunarG's redistributable: a runner has no driver, which
    on a desk installs one system-wide. Linux's is the set's, the one the AppImage carries."""

    def fill(partial: Path) -> None:
        archive = _download_into(partial, pins.VULKAN_LOADER_WINDOWS)
        # The x64 one by its path: the archive holds each architecture's loader under one name.
        with zipfile.ZipFile(archive) as opened, opened.open(f"{archive.stem}/x64/vulkan-1.dll") as source, \
                (partial / "vulkan-1.dll").open("wb") as out:
            shutil.copyfileobj(source, out)
        archive.unlink()

    return _fetched_once(vulkan_loader_dir(), fill)


def vulkan_environment(env: dict[str, str]) -> None:
    """**Where the Vulkan headers, the shader tools and the loader stand**: the set, whose path the presets read
    from `OMW_VCPKG_SET`, and the loader the tests load, the set's on Linux and LunarG's on Windows."""
    vcpkg = held_set_dir() / TRIPLET
    env[environment_key("OMW_VCPKG_SET")] = str(vcpkg)
    if WINDOWS:
        prepend_path(env, "PATH", vulkan_loader_dir())
    else:
        prepend_path(env, "LD_LIBRARY_PATH", vcpkg / "installed" / TRIPLET / "lib")


def pinned_names() -> set[str]:
    """Every name in deps/ that the pins give this system, through the functions that name each getter's folder,
    so pruning cannot take what a getter fetched."""
    kept = [cmake_dir(), ninja_dir(), clang_format_dir(), gersemi_dir(), vcpkg_root_dir(), appimage_tools_dir(),
            llvm_dir(), VCPKG_BINARY_CACHE, VCPKG_DOWNLOADS, VCPKG_WORK, VCPKG_SETS,
            held_set_dir(), vcpkg_set_dir(set_digest()), vcpkg_set_dir(pins.VCPKG_SET.digest)]
    kept += [crash_tool_dir(name) for name, system in pins.CRASH_TOOLS if system == SYSTEM]
    if WINDOWS:
        kept.append(vulkan_loader_dir())
    return {path.name for path in kept}


def cached_tools() -> list[Path]:
    """**The tools CI keeps between runs** (`.github/actions/openmw-deps`), small and fetched by every job, so a
    cache keyed by these names moves with a tool and nothing else. Not the set, gigabytes, which a job downloads."""
    tools = [cmake_dir(), ninja_dir(), llvm_dir()]
    if WINDOWS:
        tools.append(vulkan_loader_dir())
    return tools


def prune() -> None:
    """**What deps/ holds that no pin names, removed**: a version replaced, and a partial a stopped fetch left, so
    a cache restored from an older run sheds it."""
    if not DEPS.is_dir():
        return
    keep = pinned_names()
    stale = [entry for entry in DEPS.iterdir() if entry.name not in keep]
    for entry in stale:
        print(f"removing {entry.relative_to(DEPS)}, which no pin names", file=sys.stderr)
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()


def bootstrap() -> None:
    """The vcpkg set, its shader tools run once to prove it, so a tool left out or missing a library stops here by
    name and not mid-configure; Windows' loader, the pinned CMake and Ninja, and deps/ pruned."""
    vcpkg = vcpkg_set()
    for tool, port in SHADER_TOOLS.items():
        found = vcpkg / "installed" / TRIPLET / "tools" / port / f"{tool}{EXE}"
        if not found.is_file():
            raise Refusal(f"the vcpkg set at {vcpkg} holds no {tool}")
        subprocess.run([found, "--version"], check=True, stdout=subprocess.DEVNULL)
    print(f"vcpkg set: {vcpkg}")
    if WINDOWS:
        print(f"Vulkan loader: {vulkan_loader()}")
    print(f"CMake: {cmake()}")
    print(f"Ninja: {ninja()}")
    prune()
