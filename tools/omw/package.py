"""`omw archive [name]`: the release archive out of the package build, into dist/, with its symbols.
An AppImage on Linux and the portable folder on Windows, named after the name given or the commit."""

import os
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

from omw import deps
from omw.build import Build
from omw.system import ROOT, SYSTEM, WINDOWS, Refusal, output, require, run

DIST = ROOT / "dist"


def used_osg_plugins(cmake_text: str) -> list[str]:
    """The names between `set(USED_OSG_PLUGINS` and its `)`, `osgdb_` and all, read off the one place
    that declares them so this list cannot drift from the engine's own check."""
    match = re.search(r"^set\(USED_OSG_PLUGINS\b(.*?)\)", cmake_text, re.MULTILINE | re.DOTALL)
    if match is None:
        raise Refusal("the top-level CMakeLists.txt sets no USED_OSG_PLUGINS")
    return match.group(1).split()


def archive(build: Build, args: list[str]) -> int:
    if len(args) > 1:
        raise Refusal("archive takes one name at most")
    build.build(build.default_targets)
    name = args[0] if args else output(["git", "-C", ROOT, "describe", "--tags", "--always"]).strip()
    DIST.mkdir(exist_ok=True)
    if WINDOWS:
        _archive_windows(build, name)
    else:
        _archive_linux(build, name)
    symbols(build, name)
    for item in sorted(DIST.iterdir()):
        print(f"{item.stat().st_size:>12} {item.name}")
    return 0


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
    backend calls. The update information points AppImageUpdate at this repository's releases.
    AppStream validation is off: upstream's metainfo carries warnings appstreamcli refuses. The tools
    are AppImages themselves and a runner has no FUSE, so they extract and run. What the image takes
    from the host is the AppImage exclude list — glibc and libstdc++, the GL stack, X11, Wayland,
    fontconfig, ALSA — at the versions of a bare Ubuntu 24.04.

    **The DLSS library is out of the AppDir while linuxdeploy runs.** Its last pass patches an rpath
    into every ELF file it finds there, and NVIDIA's feature library is signed: with an rpath added,
    NGX said the device offered no Ray Reconstruction. So the deploy pass runs without it, the file
    goes back untouched, and the image is then written by the AppImage plugin called on its own,
    which patches nothing."""
    qmake = require("qmake6", "the Qt plugin reads Qt's layout off it: qt6-base-dev-tools")
    require("pkg-config", "OSG's version and library directory come off its .pc: pkg-config")
    require("zsyncmake", "the update information wants a .zsync beside the image: zsync")
    tools = deps.appimage_tools()

    appdir = ROOT / "AppDir"
    shutil.rmtree(appdir, ignore_errors=True)
    run(["cmake", "--install", build.dir, "--prefix", appdir / "usr" / "bin"], env=build.env, stdout=subprocess.DEVNULL)
    (appdir / "usr" / "bin" / "share").rename(appdir / "usr" / "share")

    osg_version = output(["pkg-config", "--modversion", "openscenegraph-osg"]).strip()
    osg_libdir = Path(output(["pkg-config", "--variable=libdir", "openscenegraph-osg"]).strip())
    plugins = appdir / "usr" / "lib" / f"osgPlugins-{osg_version}"
    plugins.mkdir(parents=True)
    for plugin in used_osg_plugins((ROOT / "CMakeLists.txt").read_text()):
        shutil.copy2(osg_libdir / f"osgPlugins-{osg_version}" / f"{plugin}.so", plugins)
    hooks = appdir / "apprun-hooks"
    hooks.mkdir()
    (hooks / "osg-plugins.sh").write_text('export OSG_LIBRARY_PATH="$this_dir/usr/lib"\n')

    kept = ROOT / "AppDir.kept"
    shutil.rmtree(kept, ignore_errors=True)
    kept.mkdir()
    for signed in (appdir / "usr" / "bin").glob("libnvidia-ngx-*.so.*"):
        signed.rename(kept / signed.name)

    executables: list[str | Path] = []
    for file in sorted((appdir / "usr" / "bin").iterdir()):
        if file.is_file() and os.access(file, os.X_OK):
            executables += ["--executable", file]

    env = dict(build.env, APPIMAGE_EXTRACT_AND_RUN="1", QMAKE=qmake,
               EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so", EXTRA_QT_MODULES="waylandcompositor")
    env["PATH"] = os.pathsep.join([str(tools), env.get("PATH", "")])
    run([tools / "linuxdeploy", "--appdir", appdir, *executables,
         "--deploy-deps-only", plugins,
         "--desktop-file", appdir / "usr" / "share" / "applications" / "org.openmw.launcher.desktop",
         "--icon-file", appdir / "usr" / "share" / "pixmaps" / "openmw.png",
         "--plugin", "qt"], env=env)

    for signed in kept.iterdir():
        signed.rename(appdir / "usr" / "bin" / signed.name)
    kept.rmdir()

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
