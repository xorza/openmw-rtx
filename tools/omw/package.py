"""`omw archive [name]`: the release archive out of the release build, into dist/, with its symbols: an AppImage on
Linux, built on AlmaLinux 8 or in a container of it, and the portable folder on Windows, named after the name given
or the commit."""

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
from omw.build import DEFAULT_TARGETS, Build
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

# **The system a Linux release and its dependency set are built on**: AlmaLinux 8, any point release, whose glibc
# 2.28 an AppImage takes as the floor it runs on. A desk on another builds in a container of it
# (`env/release-base.Dockerfile`).
RELEASE_BASE = {"ID": "almalinux", "VERSION_ID": "8"}
RELEASE_IMAGE = "openmw-rtx-release-base"
# The file a Linux system names itself in.
OS_RELEASE = Path("/etc/os-release")

# What the container keeps between runs on the desk, so its files are the desk's own: its build, apart from the
# desk's, and its compiler cache.
CONTAINER_DIR = ROOT / "build-container"


# What the harness and the tests are and write under the build, which no install may carry, by name wherever they
# land: the one list the packages check their install against.
HARNESS_NAMES = ("rtxtool", "test-output", "crash-matrix", "views.cfg", "benches.cfg", "shaders-source",
                 "shaders-census", "shaders-census-source", "openmw-rtxtool", "rtx-gpu-tests")


@dataclass(frozen=True)
class ShippedLibraries:
    """**The shared libraries an archive holds beside its programs, which link the set in**: those `pattern`
    matches from its root, the `names` it must hold, and beside them only those of the system it is built on,
    found by name in `system`. Any other is one the static build failed to link in."""

    pattern: str
    names: tuple[str, ...]
    system: tuple[Path, ...]

    def mismatch(self, folder: Path) -> list[str]:
        """What sets `folder` apart from these: each name it lacks, then each library beyond them that is not the
        system's."""
        held = sorted(path.relative_to(folder).as_posix() for path in folder.glob(self.pattern) if path.is_file())
        return ([f"no {name}" for name in self.names if not (folder / name).is_file()]
                + [f"{name} beside them" for name in held
                   if name not in self.names and not any((root / Path(name).name).is_file() for root in self.system)])

    def refuse_mismatch(self, folder: Path) -> None:
        if mismatch := self.mismatch(folder):
            raise Refusal("the archive's libraries are not the ones its programs load: " + ", ".join(mismatch))


# **What the AppImage leaves to the host beyond the exclude list**: libxkbcommon, which reads the host's own keymaps
# and so must be as new as they are, and its X11 half, which is only ever the same release — the image's 0.9 beside
# a host's newer core crashed every Qt program on X11.
LINUX_HOST_LIBRARIES = ("libxkbcommon.so*", "libxkbcommon-x11.so*")

# Linux carries the set's 1.4 Vulkan loader, which volk opens by name so linuxdeploy never sees it, and the release
# base's own libraries Qt's platforms ask for; Windows carries Crashpad's module, which Windows Error Reporting loads
# by path.
LINUX_LOADER = "usr/lib/libvulkan.so.1"
LINUX_LIBRARIES = ShippedLibraries("usr/lib/**/*.so*", (LINUX_LOADER,), (Path("/usr/lib64"), Path("/lib64")))
WINDOWS_LIBRARIES = ShippedLibraries("*.dll", ("openmw-wer.dll",), ())


# The newest glibc an AppImage may ask of its host: AlmaLinux 8's, the release base's.
GLIBC_FLOOR = (2, 28)


def glibc_versions(objdump: str) -> set[tuple[int, ...]]:
    """The glibc symbol versions `objdump -T` lists, as numbers: what a binary asks of the host's glibc."""
    return {tuple(int(part) for part in found.split("."))
            for found in re.findall(r"\bGLIBC_([0-9]+(?:\.[0-9]+)+)\b", objdump)}


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


def needs_container() -> bool:
    """Whether a build that runs on the release base is made in its container: on every Linux desk but the
    release base itself, and on a system with no `/etc/os-release` to say which it is."""
    return not on_release_base(read_text(OS_RELEASE) if OS_RELEASE.is_file() else "")


def container_command(docker: str, command: list[str], user: str, flavour: str) -> list[str]:
    """`command` in the release-base container as `user`, `uid:gid`, with the tree where it stands on the desk, so
    every path is the same inside, and the container's own build of `flavour` and compiler cache in place of the
    desk's."""
    return [docker, "run", "--rm", "--user", user, "--env", "HOME=/tmp",
            "--env", f"CCACHE_DIR={CONTAINER_DIR / 'ccache'}",
            # Link-time optimisation where the caller asks for it, as a release does.
            "--env", "OMW_LTO",
            "--volume", f"{ROOT}:{ROOT}",
            *(word for repository in _repository_beside() for word in ("--volume", f"{repository}:{repository}")),
            "--volume", f"{CONTAINER_DIR / flavour}:{ROOT / f'build-{flavour}'}",
            "--workdir", str(ROOT), RELEASE_IMAGE, *command]


def _repository_beside() -> list[Path]:
    """The repository a worktree keeps outside the tree, which git in the container reads through `.git`."""
    common = Path(output(["git", "-C", ROOT, "rev-parse", "--path-format=absolute", "--git-common-dir"]).strip())
    return [] if common.is_relative_to(ROOT) else [common]


@dataclass(frozen=True)
class Container:
    """The release-base container, ready to run commands over the container's build of `flavour`."""
    docker: str
    user: str
    flavour: str

    def command(self, command: list[str]) -> list[str]:
        return container_command(self.docker, command, self.user, self.flavour)


def release_container(flavour: str) -> Container:
    """The release-base image made, a moment's work after the first, and the container's build of `flavour` given
    a folder."""
    docker = require("docker", "a release is built on AlmaLinux 8, and on another system in a container: docker")
    run([docker, "build", "--tag", RELEASE_IMAGE, "--file", ROOT / "env" / "release-base.Dockerfile", ROOT / "env"])
    for kept in (flavour, "ccache"):
        (CONTAINER_DIR / kept).mkdir(parents=True, exist_ok=True)
    return Container(docker, user_and_group(), flavour)


def prepare_container(flavour: str) -> Container:
    """`release_container`, with what `omw bootstrap` fetches into the deps/ the desk shares."""
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
    """The programs directly in `folder`: its `.exe` files on Windows, elsewhere its executable files but a shared
    library."""
    if WINDOWS:
        return sorted(folder.glob("*.exe"))
    return sorted(file for file in folder.iterdir()
                  if file.is_file() and os.access(file, os.X_OK) and ".so" not in file.name)


def installed_programs(folder: Path) -> list[Path]:
    """**The programs of the build in `folder` that its last install carried**, by the names its install manifest
    lists: the ones a release ships, none of the tests or tools beside them."""
    manifest = folder / "install_manifest.txt"
    if not manifest.is_file():
        raise Refusal(f"{folder} holds no install manifest: its programs are the ones an install carried")
    names = {line.replace("\\", "/").rsplit("/", 1)[-1] for line in read_text(manifest).splitlines() if line}
    programs = [program for program in executables_in(folder) if program.name in names]
    if not programs:
        raise Refusal(f"the install of {folder} carried none of its programs")
    return programs


def prune_empty(folder: Path) -> None:
    """Removes every folder under `folder` that holds no file, deepest first: upstream's Windows rule installs the
    runtime folder through `FILES_MATCHING`, which makes every folder of it."""
    for path in sorted((p for p in folder.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        if not any(path.iterdir()):
            path.rmdir()


def archive(build: Build, args: list[str]) -> int:
    if len(args) > 1:
        raise Refusal("archive takes one name at most")
    # After the nearest release's tag, as the dependency sets' tags stand between them.
    name = args[0] if args else output(["git", "-C", ROOT, "describe", "--tags", "--match", "rtx-v*",
                                        "--always"]).strip()
    if WINDOWS:
        build.build(list(DEFAULT_TARGETS))
        DIST.mkdir(exist_ok=True)
        _archive_windows(build, name)
        symbols(build.dir, name)
    elif needs_container():
        # The image in the release base, the symbols out of its build here: `dump_syms` asks a newer glibc.
        run(prepare_container("release").command(["python3", "omw", "archive", name]))
        symbols(CONTAINER_DIR / "release", name)
    else:
        build.build(list(DEFAULT_TARGETS))
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
    """**The AppImage, as the format's guide makes one**: the portable layout under `usr/bin`, whose programs find
    their openmw.cfg beside themselves, the host's exclude list and `LINUX_HOST_LIBRARIES` left to the host, and the
    set's 1.4 Vulkan loader copied in where volk's `dlopen` finds it. **Stripped here by the host's `strip`**, since
    linuxdeploy's skips every binary whose rpath starts with `$` and reads no `.relr.dyn`."""
    require("zsyncmake", "the update information wants a .zsync beside the image: zsync")
    strip = require("strip", "the executables carry the release build's debug information: binutils")
    objdump = require("objdump", "the executables' glibc symbol versions are read off them: binutils")
    tools = deps.appimage_tools()

    appdir = ROOT / "AppDir"
    shutil.rmtree(appdir, ignore_errors=True)
    run(["cmake", "--install", build.dir, "--prefix", appdir / "usr" / "bin"], env=build.env,
        stdout=subprocess.DEVNULL)
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
    # Before linuxdeploy patches an rpath in; the build ID stays, which a dump is matched to its symbols by.
    run([strip, *installed])
    executables: list[str | Path] = []
    for file in installed:
        executables += ["--executable", file]

    # The tools are AppImages themselves and a runner has no FUSE, so they extract and run.
    env = dict(build.env, APPIMAGE_EXTRACT_AND_RUN="1", NO_STRIP="1")
    prepend_path(env, "PATH", tools)
    excluded: list[str] = []
    for pattern in LINUX_HOST_LIBRARIES:
        excluded += ["--exclude-library", pattern]
    run([tools / "linuxdeploy", "--appdir", appdir, *executables, *excluded,
         "--desktop-file", appdir / "usr" / "share" / "applications" / "org.openmw.launcher.desktop",
         "--icon-file", appdir / "usr" / "share" / "pixmaps" / "openmw.png"], env=env)
    LINUX_LIBRARIES.refuse_mismatch(appdir)

    # From dist/, because appimagetool writes the .zsync into the working directory. The static runtime runs on
    # FUSE 2, FUSE 3 or none; AppStream validation is off, as upstream's metainfo carries warnings it refuses.
    env.update(LDAI_RUNTIME_FILE=str(tools / "runtime-x86_64"), LDAI_NO_APPSTREAM="1",
               LDAI_UPDATE_INFORMATION=("gh-releases-zsync|xorza|openmw-rtx|latest|"
                                        "openmw-rtx-v*-linux-x86_64.AppImage.zsync"),
               LDAI_OUTPUT=str(DIST / f"openmw-{name}-linux-x86_64.AppImage"))
    run([tools / "linuxdeploy-plugin-appimage", "--appdir", appdir], env=env, cwd=DIST)
    shutil.rmtree(appdir)


def _archive_windows(build: Build, name: str) -> None:
    """**The portable folder, off the install rules upstream's installer is made of**, so a fork unpacks beside a
    stock OpenMW rather than register itself where one would. The zip around it is made where it is handed out."""
    folder = DIST / f"openmw-{name}-windows-x64"
    shutil.rmtree(folder, ignore_errors=True)
    run(["cmake", "--install", build.dir, "--prefix", folder], env=build.env, stdout=subprocess.DEVNULL)
    prune_empty(folder)
    _refuse_harness(folder)
    WINDOWS_LIBRARIES.refuse_mismatch(folder)


def symbols(folder: Path, name: str) -> None:
    """**The release's symbols**: every program the archive installed from the build in `folder`, through
    `dump_syms` into a Breakpad store zipped as `dist/openmw-<name>-<system>-symbols.zip`, which `omw crash` reads a
    player's dump against. From the build's own binaries, since the archive's are stripped."""
    dump_syms = deps.crash_tool("dump_syms")
    with tempfile.TemporaryDirectory() as store:
        for executable in installed_programs(folder):
            run([dump_syms, "--store", store, executable], stdout=subprocess.DEVNULL)
        target = DIST / f"openmw-{name}-{SYSTEM}-symbols.zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zipped:
            for file in sorted(Path(store).rglob("*")):
                if file.is_file():
                    zipped.write(file, file.relative_to(store).as_posix())
