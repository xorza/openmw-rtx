"""What a build needs and no system package brings, fetched into out/deps/ at the versions `pins.py` names. Each tool
stands in a folder named after its pins (`pinned_folder`), so a bump fetches anew and an unchanged pin fetches
nothing."""

import functools
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


def _wheel_program(folder: Path, name: str, wheel: pins.Pin) -> Path:
    """**The one program a PyPI wheel carries**, `name`, out of `wheel` into `folder`, read and never installed: no
    interpreter's package, and no version but the pin on any desk or CI."""
    program = f"{name}{EXE}"

    def fill(partial: Path) -> None:
        archive = _download_into(partial, wheel)
        fetch.extract_member(archive, program, partial)
        archive.unlink()

    return _fetched_once(folder, fill) / program


def clang_format_dir() -> Path:
    return pinned_folder("clang-format", pins.CLANG_FORMAT[SYSTEM])


def clang_format() -> Path:
    """**The clang-format every desk and CI formats with**: one layout on every system."""
    return _wheel_program(clang_format_dir(), "clang-format", pins.CLANG_FORMAT[SYSTEM])


def ruff_dir() -> Path:
    return pinned_folder("ruff", pins.RUFF[SYSTEM])


def ruff() -> Path:
    """**The Ruff every desk and CI lints the driver with.**"""
    return _wheel_program(ruff_dir(), "ruff", pins.RUFF[SYSTEM])


def zizmor_dir() -> Path:
    return pinned_folder("zizmor", pins.ZIZMOR[SYSTEM])


def zizmor() -> Path:
    """**The zizmor every desk and CI audits the workflows with.**"""
    return _wheel_program(zizmor_dir(), "zizmor", pins.ZIZMOR[SYSTEM])


def _for_interpreter(name: str, wheels: dict[tuple[str, str], pins.Pin],
                     minor: int = sys.version_info.minor) -> pins.Pin:
    """The wheel of `wheels` for this system and CPython 3.`minor`, the one running the driver, whose ABI the wheel's
    compiled part is built for; refused, naming the ones `pins.py` has, where it names none."""
    pin = wheels.get((SYSTEM, f"cp3{minor}"))
    if pin is None:
        minors = sorted(int(tag.removeprefix("cp3")) for system, tag in wheels if system == SYSTEM)
        raise Refusal(f"pins.py names no {name} wheel for Python 3.{minor} on {SYSTEM}: "
                      f"CPython 3.{minors[0]} to 3.{minors[-1]}")
    return pin


def _module_command(folder: Path, module: str, function: str) -> list[str]:
    """`function` of `module` out of the packages unpacked in `folder`, as the command that runs it: by this
    interpreter, isolated, with that folder alone on its path ahead of its own library."""
    return [sys.executable, "-I", "-c",
            f"import sys; sys.path.insert(0, sys.argv.pop(1)); from {module} import {function}; sys.exit({function}())",
            str(folder)]


def _unpack_wheels(partial: Path, wheels: list[pins.Pin]) -> None:
    for pin in wheels:
        wheel = _download_into(partial, pin)
        with zipfile.ZipFile(wheel) as zipped:
            zipped.extractall(partial)
        wheel.unlink()


def gersemi_pin() -> pins.Pin:
    return _for_interpreter("gersemi", pins.GERSEMI)


def gersemi_dir() -> Path:
    """Where this interpreter's gersemi stands: another Python's is another folder, as its wheel is."""
    return pinned_folder("gersemi", gersemi_pin(), pins.PYYAML)


def gersemi() -> list[str]:
    """**The gersemi every desk and CI formats the CMake files with**, as the command that runs it: its wheel
    unpacked beside PyYAML's pure-Python package, run by this interpreter, isolated, with that folder alone on its
    path. Read and never installed."""

    def fill(partial: Path) -> None:
        _unpack_wheels(partial, [gersemi_pin()])
        source = _download_into(partial, pins.PYYAML)
        with tarfile.open(source) as tarred:
            members = [member for member in tarred.getmembers() if "/lib/yaml/" in member.name]
            tarred.extractall(partial / "pyyaml", members=members, filter="data")
        source.unlink()
        next((partial / "pyyaml").glob("*/lib/yaml")).rename(partial / "yaml")
        shutil.rmtree(partial / "pyyaml")

    return _module_command(_fetched_once(gersemi_dir(), fill), "gersemi.__main__", "main")


def mypy_pins() -> list[pins.Pin]:
    """mypy's wheel for this system and interpreter, and those of everything it imports."""
    return [_for_interpreter("mypy", pins.MYPY), _for_interpreter("librt", pins.LIBRT), pins.AST_SERIALIZE[SYSTEM],
            *pins.MYPY_PURE]


def mypy_dir() -> Path:
    """Where this interpreter's mypy stands: another Python's is another folder, as its wheels are."""
    return pinned_folder("mypy", *mypy_pins())


def mypy() -> list[str]:
    """**The mypy every desk and CI checks the driver's types with**, as the command that runs it: its wheel and
    those of what it imports unpacked into one folder, run as gersemi is. Read and never installed."""
    return _module_command(_fetched_once(mypy_dir(), lambda partial: _unpack_wheels(partial, mypy_pins())),
                           "mypy.__main__", "console_entry")


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


def set_tool(port: str, tool: str) -> Path:
    """**A host program of the vcpkg set**, from the folder its port installs its tools in: the one the build runs,
    at the version the set pins, and never another on the PATH."""
    vcpkg = vcpkg_set()
    found = vcpkg / "installed" / TRIPLET / "tools" / port / f"{tool}{EXE}"
    if not found.is_file():
        raise Refusal(f"the vcpkg set at {vcpkg} holds no {tool}")
    return found


# The set's clang-tidy, a port of the fork's own: its programs and Clang's builtin headers stand in its tools folder.
CLANG_TIDY_PORT = "clang-tidy"


def clang_tidy() -> Path:
    """**The clang-tidy every desk and CI lints with**, the set's, built from the LLVM release the Linux builds
    compile with. With the builtin headers beside it, which it finds from its own path, and `clang-apply-replacements`
    (`clang_apply_replacements`), which `--fix` merges its edits with."""
    return set_tool(CLANG_TIDY_PORT, "clang-tidy")


def clang_apply_replacements() -> Path:
    return set_tool(CLANG_TIDY_PORT, "clang-apply-replacements")


@functools.cache
def clang_tidy_digest(root: Path = ROOT) -> str:
    """**Which clang-tidy lints**, as `omw tidy`'s store remembers it: a digest of the port it is built from, its
    LLVM release and its switches. Not the set's digest, which every other port moves too, and each move would lint
    the tree whole again."""
    digest = hashlib.sha256()
    for name in sorted(working_tree_files(f"vcpkg/ports/{CLANG_TIDY_PORT}/*", root=root)):
        digest.update(name.encode() + b"\0")
        digest.update((root / name).read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()[:16]


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
    """Every name in out/deps/ that the pins give this system, through the functions that name each getter's folder,
    so pruning cannot take what a getter fetched."""
    kept = [cmake_dir(), ninja_dir(), clang_format_dir(), gersemi_dir(), ruff_dir(), zizmor_dir(), mypy_dir(),
            vcpkg_root_dir(),
            appimage_tools_dir(), llvm_dir(), VCPKG_BINARY_CACHE, VCPKG_DOWNLOADS, VCPKG_WORK, VCPKG_SETS,
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
    """**What out/deps/ holds that no pin names, removed**: a version replaced, and a partial a stopped fetch left, so
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
    name and not mid-configure; Windows' loader, the pinned CMake and Ninja, and out/deps/ pruned."""
    vcpkg = vcpkg_set()
    for tool, port in SHADER_TOOLS.items():
        subprocess.run([set_tool(port, tool), "--version"], check=True, stdout=subprocess.DEVNULL)
    print(f"vcpkg set: {vcpkg}")
    if WINDOWS:
        print(f"Vulkan loader: {vulkan_loader()}")
    print(f"CMake: {cmake()}")
    print(f"Ninja: {ninja()}")
    prune()
