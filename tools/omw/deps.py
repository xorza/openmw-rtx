"""What a build needs and no system package brings, fetched into deps/ at the versions `pins.py`
names. Each thing is judged by the directory it leaves, named after its version, so a bump fetches
anew and an unchanged pin fetches nothing."""

import hashlib
import io
import shutil
import subprocess
import sys
import tarfile
import zipfile
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


def clang_format_dir() -> Path:
    return pinned_folder("clang-format", pins.CLANG_FORMAT[SYSTEM])


def clang_format() -> Path:
    """**The clang-format every desk and CI formats with**, the one program out of the wheel `pins.py`
    names for this system: one layout on every system, whatever the distribution packages. The wheel
    is a zip, read here and never installed."""
    folder = clang_format_dir()
    found = folder / f"clang-format{EXE}"
    if found.is_file():
        return found
    pin = pins.CLANG_FORMAT[SYSTEM]
    wheel = DEPS / Path(pin.url).name

    def fill(partial: Path) -> None:
        fetch.download_pin(pin, wheel)
        fetch.extract_member(wheel, found.name, partial)
        wheel.unlink()

    fetch.build_beside(folder, fill)
    return found


def gersemi_pin() -> pins.Pin:
    """The gersemi wheel for this system and the CPython running the driver, whose ABI its Rust is
    built for. Refused for an interpreter `pins.py` names no wheel for."""
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
    """**The gersemi every desk and CI formats the CMake files with**, as the command that runs it: the
    wheel `pins.py` names for this system and interpreter, unpacked whole beside PyYAML's pure-Python
    package and run by this same interpreter, isolated, with that folder alone added to its path. The
    wheels are zips, read here and never installed; a Python of another version is another folder."""
    pin = gersemi_pin()
    folder = gersemi_dir()
    if not (folder / "gersemi").is_dir():
        def fill(partial: Path) -> None:
            wheel = DEPS / Path(pin.url).name
            fetch.download_pin(pin, wheel)
            with zipfile.ZipFile(wheel) as zipped:
                zipped.extractall(partial)
            wheel.unlink()
            source = DEPS / Path(pins.PYYAML.url).name
            fetch.download_pin(pins.PYYAML, source)
            with tarfile.open(source) as tarred:
                members = [member for member in tarred.getmembers() if "/lib/yaml/" in member.name]
                tarred.extractall(partial / "pyyaml", members=members, filter="data")
            source.unlink()
            (next((partial / "pyyaml").glob("*/lib/yaml"))).rename(partial / "yaml")
            shutil.rmtree(partial / "pyyaml")

        fetch.build_beside(folder, fill)
    return [sys.executable, "-I", "-c",
            "import sys; sys.path.insert(0, sys.argv.pop(1)); from gersemi.__main__ import main; sys.exit(main())",
            str(folder)]


# What vcpkg keeps between runs (`omw deps-set`): built packages, which it keys by everything that built
# each, so a set rebuilds only the ports whose inputs moved; the sources it fetched; its scratch; and the
# packed sets.
VCPKG_BINARY_CACHE = DEPS / "vcpkg-binary-cache"
VCPKG_DOWNLOADS = DEPS / "vcpkg-downloads"
VCPKG_WORK = DEPS / "vcpkg-work"
VCPKG_SETS = DEPS / "sets"

# Each system's triplet, `vcpkg/triplets/<triplet>.cmake`.
TRIPLETS = {"linux": "x64-linux-openmw", "windows": "x64-windows-openmw"}
TRIPLET = TRIPLETS[SYSTEM]

# What a set is built from, by path from the root: the manifest, its configuration, the overlay ports and
# triplets, and the image Linux builds in. With the vcpkg pins, the set's digest.
SET_INPUTS = ("vcpkg.json", "vcpkg-configuration.json", "vcpkg/*", "CI/install_almalinux_deps.sh")


def set_digest(root: Path = ROOT) -> str:
    """**The set's name: a digest of everything it is built from**, the vcpkg pins among them, so a change
    to any names another set and the one published for the old inputs is never taken for it."""
    digest = hashlib.sha256()
    for pin in (pins.VCPKG_REGISTRY, *pins.VCPKG_TOOL.values()):
        digest.update(pin.sha256.encode())
    for name in sorted(working_tree_files(*SET_INPUTS, root=root)):
        digest.update(name.encode() + b"\0")
        digest.update((root / name).read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()[:16]




def vcpkg_set_dir(digest: str) -> Path:
    return DEPS / f"vcpkg-set-{digest}"


def vcpkg_set() -> Path:
    """**The set of libraries this tree builds against**, vcpkg's export for this system's triplet, which
    a build configures with through its toolchain file: the set `omw deps-set` built on this desk for the
    inputs as they stand, or else the one `pins.py` names, fetched and joined from its parts. Refused when
    the inputs moved since the pinned set and this desk built none for them: no other set is theirs."""
    digest = set_digest()
    folder = vcpkg_set_dir(digest)
    if not folder.is_dir():
        if digest != pins.VCPKG_SET.digest:
            raise Refusal(f"vcpkg.json or its overlays moved since the set pins.py names (deps-{pins.VCPKG_SET.digest}): "
                          f"`omw deps-set` builds deps-{digest} here, and the Dependency sets workflow publishes it "
                          "for pins.py")
        fetch.build_beside(folder, _fill_set)
    return folder / TRIPLET


def _fill_set(partial: Path) -> None:
    parts = []
    for pin in pins.VCPKG_SET.parts(SYSTEM, TRIPLET):
        part = DEPS / Path(pin.url).name
        fetch.download_pin(pin, part)
        parts.append(part)
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
    """**vcpkg as `pins.py` names it**: its registry at the pinned commit, unpacked, with the pinned tool
    at its root, where the registry's own bootstrap would put it. A registry without its Git history:
    the tree pins versions by the commit and overlays, never by vcpkg's version database, which needs
    the history."""
    root = vcpkg_root_dir()
    if (root / f"vcpkg{EXE}").is_file():
        return root

    def fill(partial: Path) -> None:
        archive = DEPS / Path(pins.VCPKG_REGISTRY.url).name
        fetch.download_pin(pins.VCPKG_REGISTRY, archive)
        fetch.unpack_stripped(archive, partial)
        archive.unlink()
        tool = partial / f"vcpkg{EXE}"
        fetch.download_pin(pins.VCPKG_TOOL[SYSTEM], tool)
        if not WINDOWS:
            tool.chmod(0o755)

    fetch.build_beside(root, fill)
    return root


def pinned_folder(name: str, *pinned: pins.Pin) -> Path:
    """**Where tools fetched by `pinned` stand, named after their digests**, as the SDK's folder is
    after its version: a pin changed is a folder not there yet, so a desk that has the old tool, and
    a CI cache that restores it, fetch the new one rather than keep the old under the same name."""
    digest = hashlib.sha256("".join(pin.sha256 for pin in pinned).encode()).hexdigest()[:16]
    return DEPS / f"{name}-{digest}"


def cmake_dir() -> Path:
    return pinned_folder("cmake", pins.CMAKE[SYSTEM])


def cmake() -> Path:
    """**The CMake every build of the driver configures, builds, tests and installs with**, Kitware's
    own of the version `pins.py` names, fetched once whole: one CMake on every desk, on CI, whose
    runners carry an older one, and in the release container, whose Ubuntu carries an older one still.
    Its `bin` goes first on the PATH of every command (`cmake_environment`)."""
    folder = cmake_dir()
    if folder.is_dir():
        return folder
    pin = pins.CMAKE[SYSTEM]
    archive = DEPS / Path(pin.url).name
    fetch.download_pin(pin, archive)
    # Kitware's archive holds one folder named after itself, which takes the pinned name.
    inner = archive.name.removesuffix(".zip").removesuffix(".tar.gz")

    def unpack(partial: Path) -> None:
        if WINDOWS:
            with zipfile.ZipFile(archive) as opened:
                opened.extractall(partial)
        else:
            with tarfile.open(archive) as opened:
                opened.extractall(partial, filter="data")

    fetch.build_beside(folder, unpack, within=inner)
    archive.unlink()
    return folder


def ninja_dir() -> Path:
    return pinned_folder("ninja", pins.NINJA[SYSTEM])


def ninja() -> Path:
    """**The pinned Ninja**, fetched once into a folder of its pin's own: the one every build on every system
    runs, where a system's is as old as AlmaLinux 8's 1.8.2."""
    folder = ninja_dir()
    if (folder / f"ninja{EXE}").is_file():
        return folder

    def fill(partial: Path) -> None:
        archive = partial / Path(pins.NINJA[SYSTEM].url).name
        fetch.download_pin(pins.NINJA[SYSTEM], archive)
        fetch.extract_member(archive, f"ninja{EXE}", partial)
        archive.unlink()

    return fetch.build_beside(folder, fill)


def cmake_environment(env: dict[str, str]) -> None:
    """The pinned CMake first on the PATH and the pinned Ninja named, fetched if they are not there yet:
    `cmake` and `ctest`, which every command names bare, are then CMake's own, and the presets hand
    `OMW_NINJA` to CMake as its make program, so a directory configured with another Ninja is configured
    again, as the presets' digest reads it."""
    env[environment_key("OMW_NINJA")] = str(ninja() / f"ninja{EXE}")
    prepend_path(env, "PATH", cmake() / "bin")


def llvm_dir() -> Path:
    return pinned_folder("llvm", pins.LLVM)


def llvm() -> Path:
    """**The pinned LLVM, unpacked**, with `bin/clang` and `bin/clang++`: CI's clang leg, which compiles for
    Clang's warnings with the Clang the editor's clangd is."""
    folder = llvm_dir()
    if (folder / "bin" / "clang++").is_file():
        return folder

    def fill(partial: Path) -> None:
        archive = partial.parent / Path(pins.LLVM.url).name
        fetch.download_pin(pins.LLVM, archive)
        fetch.unpack_stripped(archive, partial)
        archive.unlink()

    return fetch.build_beside(folder, fill)


def appimage_tools_dir() -> Path:
    return pinned_folder("appimage", *pins.APPIMAGE_TOOLS.values())


def appimage_tools() -> Path:
    """linuxdeploy, its AppImage plugin and the AppImage runtime, together in one folder, where
    linuxdeploy looks for its plugins: fetched beside it and given its name once all three are in."""
    tools = appimage_tools_dir()
    if tools.is_dir():
        return tools

    def fill(partial: Path) -> None:
        for name, pin in pins.APPIMAGE_TOOLS.items():
            fetch.download_pin(pin, partial / name)
            (partial / name).chmod(0o755)

    return fetch.build_beside(tools, fill)


def crash_tool_dir(name: str) -> Path:
    return pinned_folder(name, pins.CRASH_TOOLS[(name, SYSTEM)])


def crash_tool(name: str) -> Path:
    """One of Breakpad's two tools, fetched once into a folder of its pin's own: the player never has
    them, and the desk and CI take the versions `pins.py` names."""
    pin = pins.CRASH_TOOLS[(name, SYSTEM)]
    tools = crash_tool_dir(name)
    tool = tools / f"{name}{EXE}"
    if tool.is_file():
        return tool

    def fill(partial: Path) -> None:
        archive = partial / Path(pin.url).name
        fetch.download_pin(pin, archive)
        fetch.extract_member(archive, tool.name, partial)
        archive.unlink()

    fetch.build_beside(tools, fill)
    return tool


# The shader tools a build and the driver run, each in the set's tools folder of the port that installs it:
# `spirv-dis` is `omw kernels`', which names a module's constants by it.
SHADER_TOOLS = {"glslc": "shaderc", "spirv-val": "spirv-tools", "spirv-opt": "spirv-tools",
                "spirv-dis": "spirv-tools"}


def vulkan_loader_dir() -> Path:
    return pinned_folder("vulkan-loader", pins.VULKAN_LOADER_WINDOWS)


def vulkan_loader() -> Path:
    """**The Vulkan loader Windows' tests start against**, LunarG's redistributable of it: a runner has
    no driver, which on a desk installs the loader system-wide, and no program the archive ships carries
    one. Linux's is the set's, the one the AppImage carries."""
    folder = vulkan_loader_dir()
    if (folder / "vulkan-1.dll").is_file():
        return folder

    def fill(partial: Path) -> None:
        archive = partial / Path(pins.VULKAN_LOADER_WINDOWS.url).name
        fetch.download_pin(pins.VULKAN_LOADER_WINDOWS, archive)
        # The x64 one by its path: the archive holds the loader of each architecture under one name.
        with zipfile.ZipFile(archive) as opened, opened.open(f"{archive.stem}/x64/vulkan-1.dll") as source, \
                open(partial / "vulkan-1.dll", "wb") as out:
            shutil.copyfileobj(source, out)
        archive.unlink()

    fetch.build_beside(folder, fill)
    return folder


def vulkan_environment(env: dict[str, str]) -> None:
    """**Where the Vulkan headers, the shader tools and the loader every command reaches stand**: the vcpkg
    set, whose toolchain file the presets read from `OMW_VCPKG_SET` and which finds the headers and the
    tools, and the loader the tests load, the set's on Linux from `LD_LIBRARY_PATH` and LunarG's on Windows
    from the PATH, where `omw bootstrap` fetched it."""
    vcpkg = vcpkg_set_dir(set_digest()) / TRIPLET
    env[environment_key("OMW_VCPKG_SET")] = str(vcpkg)
    if WINDOWS:
        prepend_path(env, "PATH", vulkan_loader_dir())
    else:
        prepend_path(env, "LD_LIBRARY_PATH", vcpkg / "installed" / TRIPLET / "lib")


def pinned_names() -> set[str]:
    """Every name in deps/ that the pins give this system, through the same functions that name what
    each getter above leaves there, so pruning cannot take what a getter fetched."""
    kept = [cmake_dir(), ninja_dir(), clang_format_dir(), gersemi_dir(), vcpkg_root_dir(), appimage_tools_dir(), llvm_dir(),
            VCPKG_BINARY_CACHE, VCPKG_DOWNLOADS, VCPKG_WORK, VCPKG_SETS,
            vcpkg_set_dir(set_digest()), vcpkg_set_dir(pins.VCPKG_SET.digest)]
    kept += [crash_tool_dir(name) for name, system in pins.CRASH_TOOLS if system == SYSTEM]
    if WINDOWS:
        kept.append(vulkan_loader_dir())
    return {path.name for path in kept}


def prune() -> None:
    """**What deps/ holds that the pins no longer name, removed**: a version replaced, and a partial a
    stopped fetch left. Every name there is a getter's, so whatever none of them names is stale, and a
    cache restored from an older run sheds it rather than carrying it into every run after."""
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
    """The vcpkg set, its shader tools run once to prove it: a tool left out, or one that needs a library it
    did not bring, stops here by name and not in the middle of a configure. Windows' loader, the pinned
    CMake, and deps/ shed of what no pin names."""
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
