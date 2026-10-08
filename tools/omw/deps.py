"""What a build needs and no system package brings, fetched into deps/ at the versions `pins.py`
names. Each thing is judged by the directory it leaves, named after its version, so a bump fetches
anew and an unchanged pin fetches nothing."""

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
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
    on_path,
    prepend_path,
    read_text,
    run,
)


def msvc_versions() -> dict[str, str]:
    """The tags upstream's `CI/deps_versions.msvc.sh` pins, read the way its MSVC script reads them:
    the dependency set's `VCPKG_TAG` and Qt's `QT_VER`."""
    versions: dict[str, str] = {}
    for line in read_text(ROOT / "CI" / "deps_versions.msvc.sh").splitlines():
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
    lines = read_text(manifest).splitlines()
    # The second line is `sha512sum`'s: the digest, then the file, with a `*` in binary mode.
    checksum = lines[1].split(maxsplit=1) if len(lines) >= 2 else []
    if len(checksum) != 2:
        raise Refusal(f"{manifest.name} is no URL above a sha512sum line")
    url = lines[0].strip()
    sha512, name = checksum
    archive = DEPS / name.lstrip("*").strip()
    fetch.download(url, archive, sha512=sha512)

    fetch.build_beside(deps, lambda partial: fetch.unpack_7z(archive, partial))
    archive.unlink()
    return deps


def windows_qt(version: str) -> Path:
    """**Qt, for the flavours that build the launcher, the wizard and the CS.** The dependency set
    carries none, so it comes the way upstream's Windows workflow takes it: aqt, pinned by release
    and checksum, installs the version upstream pins into deps/Qt — beside its version's directory,
    which takes its name once aqt has finished, so a run stopped halfway is installed again."""
    qt = DEPS / "Qt" / version / "msvc2019_64"
    if qt.is_dir():
        return qt

    aqt = DEPS / "Qt" / "aqt_x64.exe"
    fetch.download_pin(pins.AQT, aqt)
    fetch.build_beside(qt.parent, lambda partial: run(
        [aqt, "install-qt", "windows", "desktop", version, "win64_msvc2019_64", "--outputdir", partial],
        cwd=aqt.parent), within=version)
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
    fetch.build_beside(found.parent, lambda partial: fetch.unpack_7z(package, partial, "bin/clang-format.exe",
                                                                     flat=True))
    package.unlink()
    return found


def pinned_folder(name: str, *pinned: pins.Pin) -> Path:
    """**Where tools fetched by `pinned` stand, named after their digests**, as the SDK's folder is
    after its version: a pin changed is a folder not there yet, so a desk that has the old tool, and
    a CI cache that restores it, fetch the new one rather than keep the old under the same name."""
    digest = hashlib.sha256("".join(pin.sha256 for pin in pinned).encode()).hexdigest()[:16]
    return DEPS / f"{name}-{digest}"


def appimage_tools() -> Path:
    """linuxdeploy, its two plugins and the AppImage runtime, together in one folder, where
    linuxdeploy looks for its plugins: fetched beside it and given its name once all four are in."""
    tools = pinned_folder("appimage", *pins.APPIMAGE_TOOLS.values())
    if tools.is_dir():
        return tools

    def fill(partial: Path) -> None:
        for name, pin in pins.APPIMAGE_TOOLS.items():
            fetch.download_pin(pin, partial / name)
            (partial / name).chmod(0o755)

    return fetch.build_beside(tools, fill)


def crash_tool(name: str) -> Path:
    """One of Breakpad's two tools, fetched once into a folder of its pin's own: the player never has
    them, and the desk and CI take the versions `pins.py` names."""
    pin = pins.CRASH_TOOLS[(name, SYSTEM)]
    tools = pinned_folder(name, pin)
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


# The SDK's tools a build and the driver run, kept out of it beside its headers and its loader:
# `spirv-dis` is `omw kernels`', which names a module's constants by it.
SDK_TOOLS = ("glslc", "spirv-val", "spirv-opt", "spirv-dis")


def vulkan_sdk_dir() -> Path:
    """**Named after the version and what is kept of it**, so a desk that has the SDK without a tool
    the list gained fetches it again, and `prune` takes the old folder."""
    version = pins.VULKAN_SDK_WINDOWS_VERSION if WINDOWS else pins.VULKAN_SDK_LINUX_VERSION
    kept = hashlib.sha256(",".join(SDK_TOOLS).encode()).hexdigest()[:8]
    return DEPS / f"vulkan-sdk-{version}-{kept}"


def vulkan_sdk() -> Path:
    """**The Vulkan SDK from LunarG, only what the build needs out of it.** The backend needs
    VK_KHR_shader_fma, which entered the SDK at 1.4.329, and a pinned SDK is the same headers and
    tools on every desk and runner, whatever the distribution packages. What is kept: the headers,
    SPIR-V's among them, the loader the tests start against, and `SDK_TOOLS`, which link nothing of the
    SDK's. No layers: a runner has no device to validate on, and a desk that
    validates has an SDK installed for it."""
    sdk = vulkan_sdk_dir()
    if sdk.is_dir():
        return sdk
    return fetch.build_beside(sdk, _vulkan_sdk_windows if WINDOWS else _vulkan_sdk_linux)


def _vulkan_sdk_linux(into: Path) -> None:
    """Out of the 330 MB tarball. The loader sits in a prefix of its own, `lib/VulkanLoader`, which is
    where the SDK's `setup-env.sh` points too."""
    tarball = DEPS / Path(pins.VULKAN_SDK_LINUX.url).name
    fetch.download_pin(pins.VULKAN_SDK_LINUX, tarball)
    wanted = re.compile(r"[^/]+/x86_64/(include/.*|lib/VulkanLoader/lib/libvulkan\.so.*"
                        rf"|bin/({'|'.join(re.escape(tool) for tool in SDK_TOOLS)}))")
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


def _quoted(text: str | Path) -> str:
    """`text` as a PowerShell string that expands nothing, whatever a path holds."""
    return "'" + str(text).replace("'", "''") + "'"


def _run_installer(program: Path, arguments: list[str], log: Path, what: str) -> None:
    """**A Qt Installer Framework program run unattended**, through PowerShell's wait, since it is a
    windowed program that answers nothing on a console of its own: its `--verbose` lines go to `log`,
    and an exit other than nought is refused with the errors it logged."""
    listed = ", ".join(_quoted(argument) for argument in ["--verbose", *arguments])
    started = subprocess.run(["powershell", "-NoProfile", "-Command",
                              (f"$p = Start-Process -Wait -PassThru -NoNewWindow -FilePath {_quoted(program)} "
                               f"-RedirectStandardOutput {_quoted(log)} -ArgumentList @({listed}); exit $p.ExitCode")],
                             check=False)
    if started.returncode != 0:
        logged = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
        errors = [line.split("] ", 1)[-1] for line in logged.splitlines() if "Error" in line]
        raise Refusal(f"the Vulkan SDK's {what} stopped with exit code {started.returncode}"
                      + "".join(f"\n  {error}" for error in errors))


def _vulkan_sdk_windows(into: Path) -> None:
    """**Out of a 288 MB installer that has no tarball beside it**: run unattended into a directory of
    its own, then the headers and `SDK_TOOLS` are taken out of it and the rest left behind. No import
    library: no program links the loader, which volk loads at run time. The loader the tests load
    comes from the runtime components, because the installer leaves it to the driver and a runner has
    no driver.

    **`copy_only=1` installs the files alone**, LunarG's switch for an unattended install that
    changes nothing of the system: without it, the core component asks for administrator rights,
    which a desk's shell does not have. The installer still registers what it put down as an
    installed SDK, so its maintenance tool purges it before the directory goes, or every bootstrap
    left an entry for a folder that no longer exists."""
    installer = DEPS / Path(pins.VULKAN_SDK_WINDOWS.url).name
    fetch.download_pin(pins.VULKAN_SDK_WINDOWS, installer)
    try:
        # What the purge leaves is left behind, read-only files and all.
        with tempfile.TemporaryDirectory(dir=DEPS, ignore_cleanup_errors=True) as scratch:
            full = Path(scratch) / "installed"
            _run_installer(installer, ["--root", str(full), "--accept-licenses", "--default-answer",
                                      "--confirm-command", "install", "copy_only=1"],
                          Path(scratch) / "install.log", "installer")
            try:
                tools = tuple(f"{tool}.exe" for tool in SDK_TOOLS)
                for needed in ["Include/vulkan/vulkan.h", "Include/spirv/unified1/spirv.hpp",
                               *(f"Bin/{tool}" for tool in tools)]:
                    if not (full / needed).is_file():
                        raise Refusal(f"the Vulkan SDK installer left no {needed}")
                (into / "Bin").mkdir(parents=True)
                shutil.copytree(full / "Include", into / "Include")
                for tool in tools:
                    shutil.copy2(full / "Bin" / tool, into / "Bin")
            finally:
                # A warning and not a refusal: what was copied is whole, and a refusal here would
                # stand in for the reason the copy stopped, where it stopped.
                try:
                    _run_installer(full / "maintenancetool.exe",
                                   ["--accept-licenses", "--default-answer", "--confirm-command", "purge"],
                                   Path(scratch) / "purge.log", "maintenance tool")
                except Refusal as unpurged:
                    print(f"omw: warning: {unpurged}; Windows lists a Vulkan SDK at {full}, which is gone, "
                          "until it is removed under Settings > Apps", file=sys.stderr)
    finally:
        installer.unlink(missing_ok=True)

    loader = DEPS / Path(pins.VULKAN_LOADER_WINDOWS.url).name
    fetch.download_pin(pins.VULKAN_LOADER_WINDOWS, loader)
    # The x64 one by its path: the archive holds the loader of each architecture under one name.
    dll = f"{loader.stem}/x64/vulkan-1.dll"
    target = into / "Bin" / "vulkan-1.dll"
    with zipfile.ZipFile(loader) as opened, opened.open(dll) as source, open(target, "wb") as out:
        shutil.copyfileobj(source, out)
    loader.unlink()


def sdk_environment(env: dict[str, str]) -> None:
    """**What `omw bootstrap` fetched, handed to every command**, the way the SDK's `setup-env.sh`
    would: FindVulkan reads `VULKAN_SDK` for the headers, through `cmake/FindVulkanHeaders.cmake`, and
    the tests load the loader from `LD_LIBRARY_PATH` or from the PATH beside glslc.
    Nothing where nothing was fetched: a desk with the SDK installed builds against that."""
    sdk = vulkan_sdk_dir()
    if sdk.is_dir():
        if WINDOWS:
            env[environment_key("VULKAN_SDK")] = str(sdk)
            prepend_path(env, "PATH", sdk / "Bin")
        else:
            base = sdk / "x86_64"
            env["VULKAN_SDK"] = str(base)
            prepend_path(env, "LD_LIBRARY_PATH", base / "lib" / "VulkanLoader" / "lib")
            prepend_path(env, "PATH", base / "bin")


def pinned_names() -> set[str]:
    """Every name in deps/ that the pins give this system: what each getter above leaves there."""
    names = {vulkan_sdk_dir().name, pinned_folder("appimage", *pins.APPIMAGE_TOOLS.values()).name}
    names |= {pinned_folder(name, pin).name for (name, system), pin in pins.CRASH_TOOLS.items() if system == SYSTEM}
    if WINDOWS:
        dependency_set = f"vcpkg-x64-windows-2022-{msvc_versions()['VCPKG_TAG']}"
        names |= {dependency_set, f"{dependency_set}-manifest.txt", "Qt", f"clang-format-{pins.LLVM_RELEASE}"}
    return names


def prune() -> None:
    """**What deps/ holds that the pins no longer name, removed**: a version replaced, and a partial a
    stopped fetch left. Every name there is a getter's, so whatever none of them names is stale, and a
    cache restored from an older run sheds it rather than carrying it into every run after."""
    if not DEPS.is_dir():
        return
    keep = pinned_names()
    stale = [entry for entry in DEPS.iterdir() if entry.name not in keep]
    qt = DEPS / "Qt"
    if WINDOWS and qt.is_dir():
        stale += [entry for entry in qt.iterdir() if entry.name != msvc_versions()["QT_VER"]]
    for entry in stale:
        print(f"removing {entry.relative_to(DEPS)}, which no pin names", file=sys.stderr)
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()


def bootstrap() -> None:
    """The Vulkan SDK, run once to prove it: a tool left out, or one that needs a library it did not
    bring, stops here by name and not in the middle of a configure. Then deps/ shed of what no pin
    names."""
    sdk = vulkan_sdk()
    env = dict(os.environ)
    sdk_environment(env)
    for tool in SDK_TOOLS:
        found = on_path(tool, env[environment_key("PATH")])
        if found is None:
            raise Refusal(f"{sdk} holds no {tool}")
        subprocess.run([found, "--version"], check=True, env=env, stdout=subprocess.DEVNULL)
    print(f"Vulkan SDK: {sdk}")
    prune()
