"""What a build needs and no system package brings, fetched into deps/ at the versions `pins.py`
names. Each thing is judged by the directory it leaves, named after its version, so a bump fetches
anew and an unchanged pin fetches nothing."""

import glob
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from omw import fetch, pins
from omw.system import DEPS, ROOT, SYSTEM, WINDOWS, Refusal, prepend_path, environment_key, run


def msvc_versions() -> dict[str, str]:
    """The tags upstream's `CI/deps_versions.msvc.sh` pins, read the way its MSVC script reads them:
    the dependency set's `VCPKG_TAG` and Qt's `QT_VER`."""
    versions: dict[str, str] = {}
    for line in (ROOT / "CI" / "deps_versions.msvc.sh").read_text().splitlines():
        match = re.fullmatch(r"\s*([A-Z_]+)=(\S+)\s*", line)
        if match:
            versions[match.group(1)] = match.group(2)
    for name in ("VCPKG_TAG", "QT_VER"):
        if name not in versions:
            raise Refusal(f"CI/deps_versions.msvc.sh names no {name}")
    return versions


def windows_set(tag: str) -> Path:
    """**Upstream's prebuilt dependency set, the way upstream's Windows workflow gets it.** A manifest
    on `openmw-deps` names the archive and its SHA-512 for the tag — the 2022 set, which upstream's
    own script links against Visual Studio 2026 as well. Unpacked beside its final name, which it
    takes only once the whole of it is there."""
    deps = DEPS / f"vcpkg-x64-windows-2022-{tag}"
    if deps.is_dir():
        return deps

    manifest = DEPS / f"{deps.name}-manifest.txt"
    fetch.download(f"https://gitlab.com/OpenMW/openmw-deps/-/raw/main/windows/{manifest.name}", manifest)
    lines = manifest.read_text().splitlines()
    url = lines[0].strip()
    # The second line is `sha512sum`'s: the digest, then the file, with a `*` in binary mode.
    sha512, name = lines[1].split(maxsplit=1)
    archive = DEPS / name.lstrip("*").strip()
    fetch.download(url, archive, sha512=sha512)

    partial = fetch.partial_of(deps)
    shutil.rmtree(partial, ignore_errors=True)
    fetch.unpack_7z(archive, partial)
    partial.rename(deps)
    archive.unlink()
    return deps


def windows_sdl(deps: Path) -> Path:
    """**SDL2 with Vulkan, from SDL's own release.** Upstream's prebuilt SDL2 is built without
    `SDL_VIDEO_VULKAN`, since the rasterizer never asks for a Vulkan surface, and the renderer's
    window is one: the symbols link and refuse at runtime. SDL's own package of the same version
    carries it — the same version, because vcpkg's SDL2_image loads `SDL2.dll` by name and was
    built against that one. So the set's version has to be the one `pins.py` pins beside the
    package's checksum, which SDL does not publish: a bump upstream is a bump there."""
    version = pins.SDL2_VERSION
    sdl = DEPS / f"SDL2-{version}"
    stamps = [Path(p).name for p in glob.glob(str(deps / "installed" / "vcpkg" / "info" / "sdl2_*_x64-windows.list"))]
    if stamps != [f"sdl2_{version}_x64-windows.list"]:
        raise Refusal(f"the dependency set carries {stamps} and pins.py pins SDL2 {version}")
    if sdl.is_dir():
        return sdl

    package = DEPS / Path(pins.SDL2.url).name
    fetch.download_pin(pins.SDL2, package)
    partial = fetch.partial_of(sdl)
    shutil.rmtree(partial, ignore_errors=True)
    shutil.unpack_archive(package, partial)
    (partial / f"SDL2-{version}").rename(sdl)
    shutil.rmtree(partial)
    package.unlink()
    return sdl


def windows_qt(version: str) -> Path:
    """**Qt, for the flavours that build the launcher, the wizard and the CS.** The dependency set
    carries none, so it comes the way upstream's Windows workflow takes it: aqt, pinned by release
    and checksum, installs the version upstream pins into deps/Qt."""
    qt = DEPS / "Qt" / version / "msvc2019_64"
    if qt.is_dir():
        return qt

    aqt = DEPS / "Qt" / "aqt_x64.exe"
    fetch.download_pin(pins.AQT, aqt)
    run([aqt, "install-qt", "windows", "desktop", version, "win64_msvc2019_64"], cwd=aqt.parent)
    aqt.unlink()
    return qt


def windows_clang_format() -> Path:
    """**The formatter CI pins, out of LLVM's own Windows package.** Nothing installs clang-format 14
    on Windows any more, but LLVM still serves the last 14 package, an NSIS installer 7-Zip reads
    without running, and `bin/clang-format.exe` is one file of it. LLVM publishes a signature and no
    checksum for that release, so the SHA-256 pinned is the one taken off the download this was
    written against: the same bytes every time, and nothing else."""
    found = DEPS / f"clang-format-{pins.LLVM_RELEASE}" / "clang-format.exe"
    if found.is_file():
        return found

    package = DEPS / Path(pins.LLVM.url).name
    fetch.download_pin(pins.LLVM, package)
    fetch.unpack_7z(package, found.parent, "bin/clang-format.exe", flat=True)
    package.unlink()
    return found


def appimage_tools() -> Path:
    tools = DEPS / "appimage"
    for name, pin in pins.APPIMAGE_TOOLS.items():
        tool = tools / name
        if not tool.is_file():
            fetch.download_pin(pin, tool)
            tool.chmod(0o755)
    return tools


def crash_tool(name: str) -> Path:
    """One of Breakpad's two tools, fetched once into deps/crash: the player never has them, and the
    desk and CI take the versions `pins.py` names."""
    tools = DEPS / "crash"
    tool = tools / f"{name}{'.exe' if WINDOWS else ''}"
    if tool.is_file():
        return tool

    pin = pins.CRASH_TOOLS[(name, SYSTEM)]
    archive = tools / Path(pin.url).name
    fetch.download_pin(pin, archive)
    fetch.extract_member(archive, tool.name, tools)
    archive.unlink()
    return tool


def ngx_version() -> str:
    """The NGX release `components/rtxvulkan/CMakeLists.txt` asks FindNGX for, exactly, read off it so
    the two cannot name different releases."""
    text = (ROOT / "components" / "rtxvulkan" / "CMakeLists.txt").read_text()
    match = re.search(r"^\s*find_package\(NGX ([0-9.]+) EXACT REQUIRED\)$", text, re.MULTILINE)
    if match is None:
        raise Refusal("components/rtxvulkan/CMakeLists.txt asks for no exact NGX version")
    return match.group(1)


def vulkan_sdk_dir() -> Path:
    version = pins.VULKAN_SDK_WINDOWS_VERSION if WINDOWS else pins.VULKAN_SDK_LINUX_VERSION
    return DEPS / f"vulkan-sdk-{version}"


def ngx_dir() -> Path:
    return DEPS / f"dlss-{ngx_version()}"


def vulkan_sdk() -> Path:
    """**The Vulkan SDK from LunarG, only what the build needs out of it.** The backend needs
    VK_EXT_ray_tracing_invocation_reorder, which entered the SDK at 1.4.333, and the distributions'
    packages stop short of it. What is kept: the headers, SPIR-V's among them, the loader the tests
    start against, and glslc, spirv-val and spirv-opt, which link nothing of the SDK's. No layers,
    since whatever runs the bootstrap without a device has no use for them."""
    sdk = vulkan_sdk_dir()
    if sdk.is_dir():
        return sdk
    partial = fetch.partial_of(sdk)
    shutil.rmtree(partial, ignore_errors=True)
    if WINDOWS:
        _vulkan_sdk_windows(partial)
    else:
        _vulkan_sdk_linux(partial)
    partial.rename(sdk)
    return sdk


def _vulkan_sdk_linux(into: Path) -> None:
    """Out of the 330 MB tarball. The loader sits in a prefix of its own, `lib/VulkanLoader`, which is
    where the SDK's `setup-env.sh` points too."""
    import tarfile

    tarball = DEPS / Path(pins.VULKAN_SDK_LINUX.url).name
    fetch.download_pin(pins.VULKAN_SDK_LINUX, tarball)
    wanted = re.compile(r"[^/]+/x86_64/(include/.*|lib/VulkanLoader/lib/libvulkan\.so.*|bin/(glslc|spirv-val|spirv-opt))")
    with tarfile.open(tarball) as opened:
        members: list[tarfile.TarInfo] = []
        for member in opened:
            if wanted.fullmatch(member.name):
                member.name = member.name.split("/", 1)[1]
                members.append(member)
        # The filter is 3.11.4's and later; a symlink the SDK carries points inside it either way.
        if hasattr(tarfile, "data_filter"):
            opened.extractall(into, members=members, filter="data")
        else:
            opened.extractall(into, members=members)
    tarball.unlink()


def _vulkan_sdk_windows(into: Path) -> None:
    """**Out of a 288 MB installer that has no tarball beside it**: run unattended into a directory of
    its own, through PowerShell's wait, since the installer is a windowed program; then the headers,
    the import library and the three tools are taken out of it and the rest left behind. The loader
    comes from the runtime components, because the installer leaves it to the driver and a runner
    has no driver."""
    installer = DEPS / Path(pins.VULKAN_SDK_WINDOWS.url).name
    fetch.download_pin(pins.VULKAN_SDK_WINDOWS, installer)
    # What the installer put down is left behind, read-only files and all.
    with tempfile.TemporaryDirectory(dir=DEPS, ignore_cleanup_errors=True) as scratch:
        full = Path(scratch) / "installed"
        arguments = ", ".join(f"'{a}'" for a in ("--root", str(full), "--accept-licenses", "--default-answer",
                                                 "--confirm-command", "install"))
        run(["powershell", "-NoProfile", "-Command",
             f"Start-Process -Wait -FilePath '{installer}' -ArgumentList @({arguments})"])
        tools = ("glslc.exe", "spirv-val.exe", "spirv-opt.exe")
        for needed in ["Include/vulkan/vulkan.h", "Include/spirv/unified1/spirv.hpp", "Lib/vulkan-1.lib",
                       *(f"Bin/{tool}" for tool in tools)]:
            if not (full / needed).is_file():
                raise Refusal(f"the Vulkan SDK installer left no {needed}")
        (into / "Lib").mkdir(parents=True)
        (into / "Bin").mkdir()
        shutil.copytree(full / "Include", into / "Include")
        shutil.copy2(full / "Lib" / "vulkan-1.lib", into / "Lib")
        for tool in tools:
            shutil.copy2(full / "Bin" / tool, into / "Bin")
    installer.unlink()

    loader = DEPS / Path(pins.VULKAN_LOADER_WINDOWS.url).name
    fetch.download_pin(pins.VULKAN_LOADER_WINDOWS, loader)
    import zipfile

    with zipfile.ZipFile(loader) as opened:
        with opened.open(f"{loader.stem}/x64/vulkan-1.dll") as source, open(into / "Bin" / "vulkan-1.dll", "wb") as out:
            shutil.copyfileobj(source, out)
    loader.unlink()


def ngx() -> Path:
    """**NGX, a sparse clone of NVIDIA's public repository at the tag of the release asked for**: the
    headers, the static library and the release features, and neither the debug features nor the
    other system's — about 150 MB either way."""
    checkout = ngx_dir()
    if checkout.is_dir():
        return checkout
    partial = fetch.partial_of(checkout)
    shutil.rmtree(partial, ignore_errors=True)
    run(["git", "clone", "--quiet", "--depth", "1", "--branch", f"v{ngx_version()}", "--filter=blob:none",
         "--sparse", "https://github.com/NVIDIA/DLSS.git", partial])
    paths = ["include", "lib/Windows_x86_64/rel", "lib/Windows_x86_64/x64"] if WINDOWS else ["include", "lib/Linux_x86_64/rel"]
    run(["git", "-C", partial, "sparse-checkout", "set", *paths])
    partial.rename(checkout)
    return checkout


def sdk_environment(env: dict[str, str]) -> None:
    """**What `omw bootstrap` fetched, handed to every command**, the way the SDK's `setup-env.sh`
    would: FindVulkan reads `VULKAN_SDK` for the headers and, on Linux, `CMAKE_PREFIX_PATH` for the
    loader's prefix; the tests load the loader from `LD_LIBRARY_PATH` or from the PATH beside glslc.
    FindNGX reads `NGX_ROOT`, which a person's own environment may name first. Nothing where
    nothing was fetched: a desk with the SDK installed builds against that."""
    sdk = vulkan_sdk_dir()
    if sdk.is_dir():
        if WINDOWS:
            env[environment_key("VULKAN_SDK")] = str(sdk)
            prepend_path(env, "PATH", sdk / "Bin")
        else:
            base = sdk / "x86_64"
            env["VULKAN_SDK"] = str(base)
            prepend_path(env, "CMAKE_PREFIX_PATH", base / "lib" / "VulkanLoader")
            prepend_path(env, "LD_LIBRARY_PATH", base / "lib" / "VulkanLoader" / "lib")
            prepend_path(env, "PATH", base / "bin")
    checkout = ngx_dir()
    if checkout.is_dir() and not env.get(environment_key("NGX_ROOT")):
        env[environment_key("NGX_ROOT")] = str(checkout)


def bootstrap() -> None:
    """The Vulkan SDK and NGX, each run once to prove it: a tool left out, or one that needs a library
    it did not bring, stops here by name and not in the middle of a configure."""
    sdk = vulkan_sdk()
    checkout = ngx()
    env = dict(os.environ)
    sdk_environment(env)
    for tool in ("glslc", "spirv-val", "spirv-opt"):
        found = shutil.which(tool, path=env[environment_key("PATH")])
        if found is None:
            raise Refusal(f"{sdk} holds no {tool}")
        subprocess.run([found, "--version"], check=True, env=env, stdout=subprocess.DEVNULL)
    print(f"Vulkan SDK: {sdk}\nNGX: {checkout}")
