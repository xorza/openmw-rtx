"""`omw archive [name]`: the release archive out of the package build, into dist/, with its symbols.
An AppImage on Linux, built on Ubuntu 24.04 or in a container of it, and the portable folder on Windows,
named after the name given or the commit."""

import os
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

from omw import deps, pins
from omw.build import Build
from omw.system import ROOT, SYSTEM, WINDOWS, Refusal, output, prepend_path, read_text, require, run

DIST = ROOT / "dist"

# **The system a Linux release is built on**, as `/etc/os-release` names it: CI's runner, whose glibc and
# libraries an AppImage takes as the floor it runs on. A desk on another builds the archive in a
# container of it (`CI/release-base.Dockerfile`), since an image built against a newer glibc runs only
# where that glibc is, and takes whatever else the desk's Qt holds.
RELEASE_BASE = {"ID": "ubuntu", "VERSION_ID": "24.04"}
RELEASE_IMAGE = "openmw-rtx-release-base"

# What the container keeps between runs, on the desk, so its files are the desk's own: its package
# build, apart from the desk's, and its compiler cache.
CONTAINER_DIR = ROOT / "build-container"


def used_osg_plugins(cmake_text: str) -> list[str]:
    """The names between `set(USED_OSG_PLUGINS` and its `)`, `osgdb_` and all, read off the one place
    that declares them so this list cannot drift from the engine's own check."""
    match = re.search(r"^set\(USED_OSG_PLUGINS\b(.*?)\)", cmake_text, re.MULTILINE | re.DOTALL)
    if match is None:
        raise Refusal("the top-level CMakeLists.txt sets no USED_OSG_PLUGINS")
    return match.group(1).split()


# What the harness and the tests are and write under the build, which no install may carry: the
# harness's folder (`RTX_HARNESS_DIR`) and the test output (`RTX_TEST_OUTPUT_DIR`), their files and
# folders by name wherever they land, and the two programs that never install. The one list: the
# packages check their install against it, and CI the macOS bundle.
HARNESS_NAMES = ("rtxtool", "test-output", "crash-matrix", "views.cfg", "benches.cfg", "shaders-source",
                 "shaders-census", "shaders-census-source", "openmw-rtxtool", "rtx-gpu-tests")


# **What an archive must carry that no linker names**, from the archive's root: on Linux the 1.4 Vulkan
# loader, which volk opens by name at start, so linuxdeploy never sees it; on Windows the C++ runtime
# the binaries were built against, which `InstallRequiredSystemLibraries` puts beside them. Without
# the loader a host whose own is 1.3, Ubuntu 24.04's among them, refuses the ray tracer; without the
# runtime no executable starts, and an older one installed faults inside `std::mutex::lock`.
LINUX_RUNTIME = ("usr/lib/libvulkan.so.1",)
WINDOWS_RUNTIME = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")


def missing_runtime(folder: Path, runtime: tuple[str, ...]) -> list[str]:
    """The files of `runtime` that `folder` does not hold."""
    return [name for name in runtime if not (folder / name).is_file()]


def _refuse_missing_runtime(folder: Path, runtime: tuple[str, ...]) -> None:
    if missing := missing_runtime(folder, runtime):
        raise Refusal("the archive lacks what its binaries load at run time: " + ", ".join(missing))


# The Wayland platform as each Qt names it: one plugin in Qt 6.11's base, and two in the
# `qt6-wayland` of Ubuntu 24.04's Qt 6.4.
WAYLAND_PLATFORMS = (("libqwayland.so",), ("libqwayland-generic.so", "libqwayland-egl.so"))


def wayland_platform_plugins(platforms: Path) -> tuple[str, ...]:
    """The Wayland platform plugins in Qt's `platforms` folder, as `EXTRA_PLATFORM_PLUGINS` takes
    them: the set of `WAYLAND_PLATFORMS` that stands there whole. A Qt with neither is refused, since
    a launcher with no Wayland platform opens on no session without XWayland."""
    for names in WAYLAND_PLATFORMS:
        if all((platforms / name).is_file() for name in names):
            return names
    raise Refusal(f"{platforms} holds no Wayland platform plugin: qt6-wayland, or qt6-base where Qt's base holds it")


def os_release(text: str) -> dict[str, str]:
    """The fields of an `/etc/os-release`, their quotes taken off."""
    fields = {}
    for line in text.splitlines():
        key, equals, value = line.partition("=")
        if equals and not key.startswith("#"):
            fields[key.strip()] = value.strip().strip('"\'')
    return fields


def on_release_base(text: str) -> bool:
    """Whether the system `text`, an `/etc/os-release`, describes is `RELEASE_BASE`."""
    fields = os_release(text)
    return all(fields.get(key) == value for key, value in RELEASE_BASE.items())


def container_command(docker: str, verb: list[str], user: str) -> list[str]:
    """`omw <verb>` in the release-base container, run by `docker`, as `user`, `uid:gid`: the tree
    where it stands on the desk, so every path is the same inside, with the container's package build
    and compiler cache from `CONTAINER_DIR` in place of the desk's own."""
    return [docker, "run", "--rm", "--user", user, "--env", "HOME=/tmp",
            "--env", f"CCACHE_DIR={CONTAINER_DIR / 'ccache'}",
            "--volume", f"{ROOT}:{ROOT}",
            "--volume", f"{CONTAINER_DIR / 'package'}:{ROOT / 'build-package'}",
            "--workdir", str(ROOT), RELEASE_IMAGE, "python3", "omw", *verb]


def harness_files(installed: Path) -> list[str]:
    """The files under `installed` that are the harness's or the tests', from its root, `/`-separated."""
    found = []
    for path in installed.rglob("*"):
        parts = path.relative_to(installed).parts
        if path.is_file() and any(part.removesuffix(".exe") in HARNESS_NAMES or part.endswith("-driver-cache")
                                  for part in parts):
            found.append(path.relative_to(installed).as_posix())
    return sorted(found)


def _refuse_harness(installed: Path) -> None:
    if found := harness_files(installed):
        raise Refusal("the install carries what the harness or the tests wrote: " + ", ".join(found[:5]))


def prune_empty(folder: Path) -> None:
    """Removes every folder under `folder` that holds no file, deepest first. Upstream's Windows rule
    installs the runtime folder through `FILES_MATCHING`, which makes every folder of it, the
    harness's and the tests' among them, empty."""
    for path in sorted((p for p in folder.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        if not any(path.iterdir()):
            path.rmdir()


def archive(build: Build, args: list[str]) -> int:
    if len(args) > 1:
        raise Refusal("archive takes one name at most")
    name = args[0] if args else output(["git", "-C", ROOT, "describe", "--tags", "--always"]).strip()
    if not WINDOWS and not on_release_base(read_text(Path("/etc/os-release"))):
        _archive_in_container(name)
        return 0

    build.build(build.default_targets)
    DIST.mkdir(exist_ok=True)
    if WINDOWS:
        _archive_windows(build, name)
    else:
        _archive_linux(build, name)
    symbols(build, name)
    # A folder by what it holds, since its own size says nothing of that: the Windows archive is one.
    for item in sorted(DIST.iterdir()):
        files = [item] if item.is_file() else [file for file in item.rglob("*") if file.is_file()]
        print(f"{sum(file.stat().st_size for file in files):>12} {item.name}")
    return 0


def _archive_in_container(name: str) -> None:
    """The archive `name` built in the release-base container, made first, which the layers it keeps
    make a moment's work after the first: the SDK `omw bootstrap` fetches into deps/, which the desk
    shares, and then the archive, into the desk's dist/."""
    docker = require("docker", "a release is built on Ubuntu 24.04, and on another system in a container: docker")
    run([docker, "build", "--tag", RELEASE_IMAGE,
         "--build-arg", f"CMAKE_URL={pins.CMAKE_LINUX.url}", "--build-arg", f"CMAKE_SHA256={pins.CMAKE_LINUX.sha256}",
         "--file", ROOT / "CI" / "release-base.Dockerfile", ROOT / "CI"])
    for kept in ("package", "ccache"):
        (CONTAINER_DIR / kept).mkdir(parents=True, exist_ok=True)
    user = f"{os.getuid()}:{os.getgid()}"
    for verb in (["bootstrap"], ["archive", name]):
        run(container_command(docker, verb, user))


def _archive_linux(build: Build, name: str) -> None:
    """**The AppImage, the way the format's own guide says to make one.** The portable layout goes
    under `usr/bin`, since the binaries find their openmw.cfg beside themselves and the launcher
    starts the game from its own directory; the desktop file, the icon and the metainfo move up to
    `usr/share`, where the tools and a desktop integrator look for them.

    **OSG's plugins are the one thing no linker names.** The engine loads the nine `USED_OSG_PLUGINS`
    by name at start and refuses to run without them, through OSG's own search:
    `osgPlugins-<version>/osgdb_<name>.so` under each entry of `OSG_LIBRARY_PATH`. Left to the host, a
    bundled OSG found Arch's copies and pulled Arch's `libosgSim` into a process running Ubuntu's
    `libosgUtil`. So the nine are copied under `usr/lib`, `--deploy-deps-only` brings what they link
    and sets their rpath, and an AppRun hook points OSG at them.

    The runtime at the front of the file is the static one, pinned: it runs on FUSE 2, FUSE 3 or
    none. The Qt plugin bundles the Wayland platform beside X11 and, through `waylandcompositor`, the
    shell, buffer and decoration plugins a Wayland window needs, so the launcher opens on a session
    with no XWayland. The Vulkan loader goes in, the 1.4 one the binaries were linked against: the
    loader is not the driver, and Ubuntu 24.04's 1.3 loader has none of the 1.4 entry points the
    backend calls. Its Apache-2.0 text goes in beside the licences the install put there. The update
    information points AppImageUpdate at this repository's releases.

    **The executables stripped here, by the host's own `strip`, and linuxdeploy's strip off.** Its
    strip passes over every executable whose rpath starts with `$`, which is each of ours, and would
    ship the package build's debug information: `openmw` is 352 MB with it and 47 MB without. The
    strip it carries is binutils 2.35, which reads no `.relr.dyn` and refuses every library of a
    system that packs its relocations, Arch's among them; and the libraries it would strip, a
    distribution ships stripped already. Stripped before linuxdeploy patches an rpath in, and the
    build ID stays, which a dump is matched to its symbols by.

    AppStream validation is off: upstream's metainfo carries warnings appstreamcli refuses. The tools
    are AppImages themselves and a runner has no FUSE, so they extract and run. What the image takes
    from the host is the AppImage exclude list — glibc and libstdc++, the GL stack, X11, Wayland,
    fontconfig, ALSA — at the versions of a bare Ubuntu 24.04."""
    qmake = require("qmake6", "the Qt plugin reads Qt's layout off it: qt6-base-dev-tools")
    require("pkg-config", "OSG's version and library directory come off its .pc: pkg-config")
    require("zsyncmake", "the update information wants a .zsync beside the image: zsync")
    strip = require("strip", "the executables carry the package build's debug information: binutils")
    tools = deps.appimage_tools()

    appdir = ROOT / "AppDir"
    shutil.rmtree(appdir, ignore_errors=True)
    run(["cmake", "--install", build.dir, "--prefix", appdir / "usr" / "bin"], env=build.env, stdout=subprocess.DEVNULL)
    _refuse_harness(appdir)
    (appdir / "usr" / "bin" / "share").rename(appdir / "usr" / "share")
    licenses = appdir / "usr" / "share" / "doc" / "OpenMW" / "licenses"
    shutil.copy2(ROOT / "files" / "licenses" / "Vulkan-Loader.txt", licenses)

    osg_version = output(["pkg-config", "--modversion", "openscenegraph-osg"]).strip()
    osg_libdir = Path(output(["pkg-config", "--variable=libdir", "openscenegraph-osg"]).strip())
    plugins = appdir / "usr" / "lib" / f"osgPlugins-{osg_version}"
    plugins.mkdir(parents=True)
    # Copied rather than handed to linuxdeploy, whose exclude list names the loader; what it links is
    # glibc's alone. The binaries' rpath, `$ORIGIN/../lib`, is where volk's `dlopen` then finds it.
    shutil.copy2(deps.vulkan_sdk() / "x86_64" / "lib" / "VulkanLoader" / "lib" / "libvulkan.so.1",
                 appdir / "usr" / "lib" / "libvulkan.so.1")
    for plugin in used_osg_plugins(read_text(ROOT / "CMakeLists.txt")):
        shutil.copy2(osg_libdir / f"osgPlugins-{osg_version}" / f"{plugin}.so", plugins)
    hooks = appdir / "apprun-hooks"
    hooks.mkdir()
    (hooks / "osg-plugins.sh").write_text('export OSG_LIBRARY_PATH="$this_dir/usr/lib"\n')

    installed = [file for file in sorted((appdir / "usr" / "bin").iterdir())
                 if file.is_file() and os.access(file, os.X_OK)]
    run([strip, *installed])
    executables: list[str | Path] = []
    for file in installed:
        executables += ["--executable", file]

    platforms = Path(output([qmake, "-query", "QT_INSTALL_PLUGINS"]).strip()) / "platforms"
    env = dict(build.env, APPIMAGE_EXTRACT_AND_RUN="1", QMAKE=qmake, NO_STRIP="1",
               EXTRA_PLATFORM_PLUGINS=";".join(wayland_platform_plugins(platforms)),
               EXTRA_QT_MODULES="waylandcompositor")
    prepend_path(env, "PATH", tools)
    run([tools / "linuxdeploy", "--appdir", appdir, *executables,
         "--deploy-deps-only", plugins,
         "--desktop-file", appdir / "usr" / "share" / "applications" / "org.openmw.launcher.desktop",
         "--icon-file", appdir / "usr" / "share" / "pixmaps" / "openmw.png",
         "--plugin", "qt"], env=env)
    _refuse_missing_runtime(appdir, LINUX_RUNTIME)

    # From dist/, because appimagetool writes the .zsync into the working directory and the image
    # where it is told.
    env.update(LDAI_RUNTIME_FILE=str(tools / "runtime-x86_64"), LDAI_NO_APPSTREAM="1",
               LDAI_UPDATE_INFORMATION="gh-releases-zsync|xorza|openmw-rtx|latest|openmw-rtx-v*-linux-x86_64.AppImage.zsync",
               LDAI_OUTPUT=str(DIST / f"openmw-{name}-linux-x86_64.AppImage"))
    run([tools / "linuxdeploy-plugin-appimage", "--appdir", appdir], env=env, cwd=DIST)
    shutil.rmtree(appdir)


def _archive_windows(build: Build, name: str) -> None:
    """**The portable folder, off the same install rules upstream's installer is made of** — a folder
    rather than that installer, because a fork under a renderer nobody has installed before should
    unpack beside a stock OpenMW, not register itself where one would. The zip around it is made
    where it is handed out: GitHub's own around a run's artifact, the release job's around a
    release's, so nobody downloads a zip inside a zip."""
    folder = DIST / f"openmw-{name}-windows-x64"
    shutil.rmtree(folder, ignore_errors=True)
    run(["cmake", "--install", build.dir, "--prefix", folder], env=build.env, stdout=subprocess.DEVNULL)
    prune_empty(folder)
    _refuse_harness(folder)


def symbols(build: Build, name: str) -> None:
    """**The release's symbols**: every executable the package build made, through `dump_syms` into a
    Breakpad symbol store, `<module>/<id>/<module>.sym`, zipped as
    `dist/openmw-<name>-<system>-symbols.zip`. A release publishes it beside its archive, and
    `omw crash` reads a player's dump against it. The debug information comes from the package
    presets, and it stays out of the archive."""
    dump_syms = deps.crash_tool("dump_syms")
    if WINDOWS:
        executables = sorted(build.dir.glob("*.exe"))
    else:
        executables = sorted(f for f in build.dir.iterdir()
                             if f.is_file() and os.access(f, os.X_OK) and ".so" not in f.name)
    with tempfile.TemporaryDirectory() as store:
        for executable in executables:
            run([dump_syms, "--store", store, executable], stdout=subprocess.DEVNULL)
        target = DIST / f"openmw-{name}-{SYSTEM}-symbols.zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zipped:
            for file in sorted(Path(store).rglob("*")):
                if file.is_file():
                    zipped.write(file, file.relative_to(store).as_posix())
