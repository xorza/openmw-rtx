"""`omw archive [name]`: the release archive out of the release build, into dist/, with its symbols.
An AppImage on Linux, built on AlmaLinux 8 or in a container of it, and the portable folder on Windows,
named after the name given or the commit."""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path

from omw import deps
from omw.build import Build
from omw.system import (
    ROOT,
    SYSTEM,
    WINDOWS,
    Refusal,
    output,
    prepend_path,
    read_text,
    require,
    run,
    user_and_group,
)

DIST = ROOT / "dist"

# **The system a Linux release and its dependency set are built on**, as `/etc/os-release` names it:
# AlmaLinux 8, whose glibc 2.28 an AppImage takes as the floor it runs on, with any point release. A desk on
# another builds in a container of it (`env/release-base.Dockerfile`), since a binary built against a newer
# glibc runs only where that glibc is.
RELEASE_BASE = {"ID": "almalinux", "VERSION_ID": "8"}
RELEASE_IMAGE = "openmw-rtx-release-base"

# What the container keeps between runs, on the desk, so its files are the desk's own: its package
# build, apart from the desk's, and its compiler cache.
CONTAINER_DIR = ROOT / "build-container"


# What the harness and the tests are and write under the build, which no install may carry: the
# harness's folder (`RTX_HARNESS_DIR`) and the test output (`RTX_TEST_OUTPUT_DIR`), their files and
# folders by name wherever they land, and the two programs that never install. The one list: the
# packages check their install against it.
HARNESS_NAMES = ("rtxtool", "test-output", "crash-matrix", "views.cfg", "benches.cfg", "shaders-source",
                 "shaders-census", "shaders-census-source", "openmw-rtxtool", "rtx-gpu-tests")


# **What an archive carries beside its programs, which link every library of the set in** (`vcpkg.json`): on
# Linux the 1.4 Vulkan loader, which volk opens by name at start, so linuxdeploy never sees it, and without
# which a host whose own loader is 1.3 refuses the ray tracer, and the release base's own libraries that the
# AppImage exclude list leaves to the image, which the static Qt's X11 and Wayland platforms ask for
# (`libxcb-cursor`, which many hosts lack and Qt 6 opens no X11 window without, `libxkbcommon-x11` and the
# rest); on Windows Crashpad's error-reporting module, which Windows Error Reporting loads by path. A library
# the archive holds beyond these is one the static build failed to link in, and the program would load the
# host's.
@dataclass(frozen=True)
class ShippedLibraries:
    """The shared libraries an archive holds: those `pattern` matches from its root, the `names` it must hold,
    and beside them those of the system it is built on alone, found by name in its folders `system`."""

    pattern: str
    names: tuple[str, ...]
    system: tuple[Path, ...]

    def mismatch(self, folder: Path) -> list[str]:
        """What sets `folder` apart from these: each name it lacks, then each library it holds beyond them that
        is not the system's."""
        held = sorted(path.relative_to(folder).as_posix() for path in folder.glob(self.pattern) if path.is_file())
        return ([f"no {name}" for name in self.names if not (folder / name).is_file()]
                + [f"{name} beside them" for name in held
                   if name not in self.names and not any((root / Path(name).name).is_file() for root in self.system)])

    def refuse_mismatch(self, folder: Path) -> None:
        if mismatch := self.mismatch(folder):
            raise Refusal("the archive's libraries are not the ones its programs load: " + ", ".join(mismatch))


# **What the AppImage leaves to the host beyond the exclude list**, as linuxdeploy patterns: libxkbcommon, which
# reads the host's own keymaps and Compose files, and so must be as new as they are. AlmaLinux 8's 0.9 in the
# image predates the keysyms a current host's Compose files name, and every Qt program printed "unrecognized
# keysym" for each; SDL loads the host's by design. Its X11 half stays in the image, which hosts without Qt lack.
LINUX_HOST_LIBRARIES = ("libxkbcommon.so*",)

LINUX_LOADER = "usr/lib/libvulkan.so.1"
# The release base's library folders, where the archive is built (`on_release_base`).
LINUX_LIBRARIES = ShippedLibraries("usr/lib/**/*.so*", (LINUX_LOADER,), (Path("/usr/lib64"), Path("/lib64")))
WINDOWS_LIBRARIES = ShippedLibraries("*.dll", ("openmw-wer.dll",), ())


# The newest glibc an AppImage may ask of its host: AlmaLinux 8's, the release base's.
GLIBC_FLOOR = (2, 28)


def glibc_versions(objdump: str) -> set[tuple[int, ...]]:
    """The glibc symbol versions `objdump -T` lists, as numbers: what a binary asks of the host's glibc."""
    return {tuple(int(part) for part in found.split(".")) for found in re.findall(r"\bGLIBC_([0-9]+(?:\.[0-9]+)+)\b", objdump)}


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
    return (fields.get("ID") == RELEASE_BASE["ID"]
            and fields.get("VERSION_ID", "").split(".")[0] == RELEASE_BASE["VERSION_ID"])


def container_command(docker: str, command: list[str], user: str, flavour: str) -> list[str]:
    """`command` in the release-base container, run by `docker`, as `user`, `uid:gid`: the tree
    where it stands on the desk, so every path is the same inside, with the container's own build of
    `flavour` and its compiler cache from `CONTAINER_DIR` in place of the desk's."""
    return [docker, "run", "--rm", "--user", user, "--env", "HOME=/tmp",
            "--env", f"CCACHE_DIR={CONTAINER_DIR / 'ccache'}",
            # Link-time optimisation where the caller asks for it, as a release does.
            "--env", "OMW_LTO",
            "--volume", f"{ROOT}:{ROOT}",
            *(word for repository in _repository_beside() for word in ("--volume", f"{repository}:{repository}")),
            "--volume", f"{CONTAINER_DIR / flavour}:{ROOT / f'build-{flavour}'}",
            "--workdir", str(ROOT), RELEASE_IMAGE, *command]


def _repository_beside() -> list[Path]:
    """The repository a worktree of it keeps outside the tree, which git inside the container reads
    through the tree's `.git` file: nothing for a tree that holds its own."""
    common = Path(output(["git", "-C", ROOT, "rev-parse", "--path-format=absolute", "--git-common-dir"]).strip())
    return [] if common.is_relative_to(ROOT) else [common]


def needs_container() -> bool:
    """Whether a build that runs on the release base is made in its container: on every Linux
    desk but the release base itself, whose own build already is one."""
    return not on_release_base(read_text(Path("/etc/os-release")))


@dataclass(frozen=True)
class Container:
    """The release-base container, ready to run commands over the container's build of `flavour`."""
    docker: str
    user: str
    flavour: str

    def command(self, command: list[str]) -> list[str]:
        return container_command(self.docker, command, self.user, self.flavour)


def release_container(flavour: str) -> Container:
    """The release-base image made, which the layers it keeps make a moment's work after the first, and
    the container's build of `flavour` given a folder."""
    docker = require("docker", "a release is built on AlmaLinux 8, and on another system in a container: docker")
    run([docker, "build", "--tag", RELEASE_IMAGE, "--file", ROOT / "env" / "release-base.Dockerfile", ROOT / "env"])
    for kept in (flavour, "ccache"):
        (CONTAINER_DIR / kept).mkdir(parents=True, exist_ok=True)
    return Container(docker, user_and_group(), flavour)


def prepare_container(flavour: str) -> Container:
    """`release_container`, with the SDK and CMake `omw bootstrap` fetches into deps/, which the desk
    shares, for a build of the tree."""
    container = release_container(flavour)
    run(container.command(["python3", "omw", "bootstrap"]))
    return container


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


def executables_in(folder: Path) -> list[Path]:
    """The programs directly in `folder`: its `.exe` files on Windows, and elsewhere its executable
    files but a shared library, which a build directory holds beside them."""
    if WINDOWS:
        return sorted(folder.glob("*.exe"))
    return sorted(file for file in folder.iterdir()
                  if file.is_file() and os.access(file, os.X_OK) and ".so" not in file.name)


def installed_programs(folder: Path) -> list[Path]:
    """**The programs of the build in `folder` that its last install carried**, by the names `cmake --install`
    listed in its manifest: the ones a release ships, which `cmake/Install.cmake` names, and none of the tests,
    the harness or the build's own tools beside them."""
    manifest = folder / "install_manifest.txt"
    if not manifest.is_file():
        raise Refusal(f"{folder} holds no install manifest: its programs are the ones an install carried")
    names = {line.replace("\\", "/").rsplit("/", 1)[-1] for line in read_text(manifest).splitlines() if line}
    programs = [program for program in executables_in(folder) if program.name in names]
    if not programs:
        raise Refusal(f"the install of {folder} carried none of its programs")
    return programs


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
    # After the nearest release's tag, as the dependency sets' tags stand between them.
    name = args[0] if args else output(["git", "-C", ROOT, "describe", "--tags", "--match", "rtx-v*", "--always"]).strip()
    if WINDOWS:
        build.build(build.default_targets)
        DIST.mkdir(exist_ok=True)
        _archive_windows(build, name)
        symbols(build.dir, name)
    elif needs_container():
        # The image in the release base, the symbols out of its build here: `dump_syms` asks a newer glibc than
        # the release base's.
        run(prepare_container("release").command(["python3", "omw", "archive", name]))
        symbols(CONTAINER_DIR / "release", name)
    else:
        build.build(build.default_targets)
        DIST.mkdir(exist_ok=True)
        _archive_linux(build, name)
        print("no symbols: dump_syms asks a newer glibc than the release base's, so a host outside it makes them "
              "from this build", file=sys.stderr)
    # A folder by what it holds, since its own size says nothing of that: the Windows archive is one.
    for item in sorted(DIST.iterdir()):
        files = [item] if item.is_file() else [file for file in item.rglob("*") if file.is_file()]
        print(f"{sum(file.stat().st_size for file in files):>12} {item.name}")
    return 0


def _archive_linux(build: Build, name: str) -> None:
    """**The AppImage, the way the format's own guide says to make one.** The portable layout goes
    under `usr/bin`, since the binaries find their openmw.cfg beside themselves and the launcher
    starts the game from its own directory; the desktop file, the icon and the metainfo move up to
    `usr/share`, where the tools and a desktop integrator look for them.

    **The programs link every library in**, the vcpkg set's, OpenSceneGraph's plugins and Qt's among them,
    and leave the host what it opens for them: the AppImage exclude list — glibc, the GL stack, X11,
    Wayland, fontconfig, ALSA — at the versions of AlmaLinux 8, the release base, and libxkbcommon
    (`LINUX_HOST_LIBRARIES`). The image carries the rest
    of what they open, the release base's own, as linuxdeploy brings it (`LINUX_LIBRARIES`), and the Vulkan
    loader, the set's 1.4 one: the loader is not the driver, and a host's 1.3
    loader has none of the 1.4 entry points the backend calls. Copied rather than handed to linuxdeploy,
    whose exclude list names it; the rpath linuxdeploy gives the binaries, `$ORIGIN/../lib`, is where
    volk's `dlopen` finds it. Its Apache-2.0 text goes in beside the licences the install put there.

    The runtime at the front of the file is the static one, pinned: it runs on FUSE 2, FUSE 3 or none.
    The update information points AppImageUpdate at this repository's releases.

    **The executables stripped here, by the host's own `strip`, and linuxdeploy's strip off.** Its
    strip passes over every executable whose rpath starts with `$`, which is each of ours, and would
    ship the release build's debug information: `openmw` is 352 MB with it and 47 MB without. The
    strip it carries is binutils 2.35, which reads no `.relr.dyn`. Stripped before linuxdeploy patches
    an rpath in, and the build ID stays, which a dump is matched to its symbols by.

    AppStream validation is off: upstream's metainfo carries warnings appstreamcli refuses. The tools
    are AppImages themselves and a runner has no FUSE, so they extract and run."""
    require("zsyncmake", "the update information wants a .zsync beside the image: zsync")
    strip = require("strip", "the executables carry the release build's debug information: binutils")
    objdump = require("objdump", "the executables' glibc symbol versions are read off them: binutils")
    tools = deps.appimage_tools()

    appdir = ROOT / "AppDir"
    shutil.rmtree(appdir, ignore_errors=True)
    run(["cmake", "--install", build.dir, "--prefix", appdir / "usr" / "bin"], env=build.env, stdout=subprocess.DEVNULL)
    _refuse_harness(appdir)
    (appdir / "usr" / "bin" / "share").rename(appdir / "usr" / "share")
    licenses = appdir / "usr" / "share" / "doc" / "OpenMW" / "licenses"
    shutil.copy2(ROOT / "files" / "licenses" / "Vulkan-Loader.txt", licenses)
    (appdir / LINUX_LOADER).parent.mkdir(parents=True)
    shutil.copy2(deps.vcpkg_set() / "installed" / deps.TRIPLET / "lib" / "libvulkan.so.1", appdir / LINUX_LOADER)

    installed = executables_in(appdir / "usr" / "bin")
    asked = max(glibc_versions(output([objdump, "-T", *installed])), default=(0,))
    if asked > GLIBC_FLOOR:
        raise Refusal(f"the programs ask glibc {'.'.join(map(str, asked))}, newer than the release base's "
                      f"{'.'.join(map(str, GLIBC_FLOOR))}: build the archive on AlmaLinux 8")
    run([strip, *installed])
    executables: list[str | Path] = []
    for file in installed:
        executables += ["--executable", file]

    env = dict(build.env, APPIMAGE_EXTRACT_AND_RUN="1", NO_STRIP="1")
    prepend_path(env, "PATH", tools)
    excluded: list[str] = []
    for pattern in LINUX_HOST_LIBRARIES:
        excluded += ["--exclude-library", pattern]
    run([tools / "linuxdeploy", "--appdir", appdir, *executables, *excluded,
         "--desktop-file", appdir / "usr" / "share" / "applications" / "org.openmw.launcher.desktop",
         "--icon-file", appdir / "usr" / "share" / "pixmaps" / "openmw.png"], env=env)
    LINUX_LIBRARIES.refuse_mismatch(appdir)

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
    WINDOWS_LIBRARIES.refuse_mismatch(folder)


def symbols(folder: Path, name: str) -> None:
    """**The release's symbols**: every program the archive installed from the release build in `folder`
    (`installed_programs`), through `dump_syms` into a Breakpad symbol store, `<module>/<id>/<module>.sym`, zipped as
    `dist/openmw-<name>-<system>-symbols.zip`. A release publishes it beside its archive, and
    `omw crash` reads a player's dump against it. The debug information is what
    `cmake/Flags.cmake` gives every configuration, read from the build's own binaries: the archive's are
    stripped on Linux, and Windows' install leaves a Release build's PDBs out."""
    dump_syms = deps.crash_tool("dump_syms")
    with tempfile.TemporaryDirectory() as store:
        for executable in installed_programs(folder):
            run([dump_syms, "--store", store, executable], stdout=subprocess.DEVNULL)
        target = DIST / f"openmw-{name}-{SYSTEM}-symbols.zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zipped:
            for file in sorted(Path(store).rglob("*")):
                if file.is_file():
                    zipped.write(file, file.relative_to(store).as_posix())
