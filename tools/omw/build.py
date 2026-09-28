"""A flavour's build directory: configured from its preset, built, and asked what it holds.

**How each flavour is configured is `CMakePresets.json`'s**, in the form CMake and every IDE read: a
flavour on a system is the preset `<flavour>-<system>`, and the reasons stand beside each variable
there. What this adds is what a preset cannot say — which system it is on and what that system
lacks, when a directory must be configured again, and what each flavour builds by default."""

import json
import os
import shutil
import subprocess
from pathlib import Path

from omw import deps, pins, presets
from omw.system import (
    EXE,
    ROOT,
    SYSTEM,
    WINDOWS,
    Refusal,
    environment_key,
    jobs,
    msvc_environment,
    output,
    resolved,
    run,
)

FLAVOURS = ("debug", "release", "asan", "nodlss", "package", "plain")

# The flavours that build the programs Qt draws: the launcher and the wizard, and the CS.
QT_FLAVOURS = ("package", "plain")

# The file a directory keeps the digest it was last configured from in.
CONFIGURED_FROM = "omw-preset.sha256"
# The file a directory keeps the NGX entries it carries across a fresh configure in.
CARRIED = "omw-carried.txt"


def carried_ngx(entries: list[str]) -> list[str]:
    """The `-D` that carries each `NGX_` entry holding a value across a fresh configure: a path, a
    file or a string that was found, and nothing CMake keeps for itself."""
    carried = []
    for entry in entries:
        name_type, _, value = entry.partition("=")
        name, _, kind = name_type.partition(":")
        if name.startswith("NGX_") and kind in ("PATH", "FILEPATH", "STRING", "UNINITIALIZED") \
                and value and not value.endswith("-NOTFOUND"):
            carried.append(f"-D{entry}")
    return carried


def configured_from(directory: Path, digest: str) -> bool:
    """Whether `directory` stands configured from the preset `digest` names, so a build there need
    only bring what CMake wrote up to date: the stamp of a configure that succeeded from that digest,
    and both halves of what the configure left — the cache the regeneration reads and the Ninja file
    it writes. A directory missing either is configured again, whatever its stamp says."""
    stamp = directory / CONFIGURED_FROM
    return (stamp.is_file() and stamp.read_text().strip() == digest
            and (directory / "CMakeCache.txt").is_file() and (directory / "build.ninja").is_file())


class Build:
    def __init__(self, flavour: str):
        if flavour not in FLAVOURS:
            raise Refusal(f"no flavour is called {flavour!r}: {', '.join(FLAVOURS)}")
        if flavour == "asan" and WINDOWS:
            raise Refusal("asan is Linux's here: MSVC's sanitizer has not been measured on this tree")
        self.flavour = flavour
        self.dir = ROOT / f"build-{flavour}"
        self.preset = f"{flavour}-{SYSTEM}"
        self._env: dict[str, str] | None = None

    @property
    def default_targets(self) -> list[str]:
        """What `build` builds when the line names nothing, beside the tests the build has."""
        if self.flavour in ("package", "plain"):
            return ["all"]
        return ["openmw-rtxtool", "openmw"]

    @property
    def env(self) -> dict[str, str]:
        """The environment every command of this flavour runs under: the SDKs `omw bootstrap`
        fetched, MSVC's and the versions the Windows presets read, and the test preset's own —
        the sanitizers' options, without which the asan build has no device."""
        if self._env is None:
            env = dict(os.environ)
            deps.sdk_environment(env)
            if WINDOWS:
                env = msvc_environment(env)
                versions = deps.msvc_versions()
                env["VCPKG_TAG"] = versions["VCPKG_TAG"]
                env["QT_VER"] = versions["QT_VER"]
                env["RTX_SDL2_VERSION"] = pins.SDL2_VERSION
            if presets.has_test_preset(self.preset):
                env = presets.test_environment(self.preset, env)
            self._env = env
        return self._env

    def cache_entries(self) -> list[str]:
        """The build's CMake cache, an entry a line; none where it has no cache yet."""
        cache = self.dir / "CMakeCache.txt"
        return cache.read_text(errors="replace").splitlines() if cache.is_file() else []

    def cache_value(self, name: str) -> str | None:
        for entry in self.cache_entries():
            key, _, value = entry.partition("=")
            if key.split(":", 1)[0] == name:
                return value
        return None

    def configure(self, stdout=None) -> None:
        """**Configured again whenever what its preset expands from changes, and from nothing
        else.** A directory configured once and never again keeps the cache it was first given, so a
        preset edited later never reached it: `build-release` went on building the tests its preset
        had since turned off. **The stamp says a configure succeeded, and nothing more**: removed as
        one starts and written once it has succeeded, so one that failed or was stopped is tried
        again rather than handed to Ninja — a fresh configure drops the cache first, and an old
        stamp beside no cache sent the build to a regeneration with nothing to regenerate from.

        **Fresh, and not the preset laid over the old cache**: a variable a preset stops naming would
        otherwise keep the value it last set. What is carried over is NGX, as every `NGX_` entry the
        cache holds a value in — `NGX_ROOT` where a `-D` named the checkout, and what FindNGX found
        with it — and only where the environment names no checkout of its own, which is then the
        one to find.

        **Neither `--clean-first` nor `ninja -t cleandead`**: files/lang/*.ts are source that a Qt
        translation rule writes into the tree, so Ninja logs them as outputs, and both delete them."""
        env = self.env
        digest = presets.digest(env)
        stamp = self.dir / CONFIGURED_FROM
        if configured_from(self.dir, digest):
            # What CMake wrote is brought up to date with the CMake files as Ninja would before a
            # build, so what is asked of it before one — the tests it has — is not a stale answer.
            run(["cmake", "--build", self.dir, "--target", "build.ninja"], env=env, stdout=subprocess.DEVNULL)
            return

        # Written down before the configure starts, since a fresh one that fails has already
        # dropped the cache it read them from, and the next would carry nothing.
        carried: list[str] = []
        remembered = self.dir / CARRIED
        if not env.get(environment_key("NGX_ROOT")):
            carried = carried_ngx(self.cache_entries())
            if carried:
                self.dir.mkdir(exist_ok=True)
                remembered.write_text("\n".join(carried) + "\n")
            elif remembered.is_file():
                carried = remembered.read_text().split("\n")[:-1]

        if WINDOWS:
            windows_deps = deps.windows_set(env["VCPKG_TAG"])
            sdl = deps.windows_sdl(windows_deps)
            qt = deps.windows_qt(env["QT_VER"]) if self.flavour in QT_FLAVOURS else None

        stamp.unlink(missing_ok=True)
        run(["cmake", "-S", ROOT, "--preset", self.preset, "--fresh", *carried], env=env, stdout=stdout)

        if WINDOWS:
            self._place_windows_runtime(windows_deps, sdl)
            if qt is not None:
                self._place_windows_qt(qt)

        stamp.write_text(digest + "\n")

    def _place_windows_runtime(self, windows_deps: Path, sdl: Path) -> None:
        """**What vcpkg's own copy step misses, placed the way upstream's MSVC script places it.**
        The toolchain copies each DLL a binary links beside it and stops there: MyGUI's sits a
        directory deeper than it looks and needs FreeType, which needs four more, and OSG's plugins
        are loaded by name at runtime from `osgPlugins-3.6.5/`, which no link line names. The whole
        set, 84 MB, rather than a list that goes stale with the next tag."""
        bin_dir = windows_deps / "installed" / "x64-windows" / "bin"
        for dll in [*bin_dir.glob("*.dll"), bin_dir / "Release" / "MyGUIEngine.dll", sdl / "lib" / "x64" / "SDL2.dll"]:
            shutil.copy2(dll, self.dir)
        plugins = self.dir / "osgPlugins-3.6.5"
        plugins.mkdir(exist_ok=True)
        for dll in (bin_dir / "osgPlugins-3.6.5").glob("*.dll"):
            shutil.copy2(dll, plugins)

    def _place_windows_qt(self, qt: Path) -> None:
        """**What the Qt programs load, the list upstream's Windows workflow places**: the seven
        libraries and four plugins the launcher, the wizard and the CS reach, where Qt's own deploy
        tool would bring the whole of what it guesses at."""
        for library in ("Core", "Gui", "Network", "OpenGL", "OpenGLWidgets", "Widgets", "Svg"):
            shutil.copy2(qt / "bin" / f"Qt6{library}.dll", self.dir)
        for kind, plugin in (("styles", "qwindowsvistastyle"), ("platforms", "qwindows"),
                             ("imageformats", "qsvg"), ("iconengines", "qsvgicon")):
            (self.dir / kind).mkdir(exist_ok=True)
            shutil.copy2(qt / "plugins" / kind / f"{plugin}.dll", self.dir / kind)

    def build(self, targets: list[str], stdout=None) -> None:
        """`-k 0` keeps Ninja going past a failed object, so one run of a build that breaks names
        every object that broke — on a compiler this box does not have, that is the difference
        between one round trip through CI and five."""
        self.configure(stdout)
        run(["cmake", "--build", self.dir, f"-j{jobs()}", "--target", *targets, "--", "-k", "0"], env=self.env,
            stdout=stdout)

    def tests(self) -> list[dict]:
        """**The tests this build has, asked of its own configuration** through CTest, and not of a
        binary on disk: a binary on disk is only what some earlier build left there."""
        self.configure()
        listing = output(["ctest", "--test-dir", self.dir, "--show-only=json-v1"], env=self.env)
        return json.loads(listing).get("tests", [])

    def test_targets(self) -> list[str]:
        """The targets the tests run, which `cmake/Tests.cmake` names on each: a test whose binary is
        not built yet has no command in CTest's listing."""
        return sorted({property["value"] for test in self.tests() for property in test.get("properties", [])
                       if property["name"] == "OPENMW_TARGET"})

    def binary(self, name: str) -> Path:
        return self.dir / f"{name}{EXE}"

    def run_here(self, command: list, cwd: Path | None = None, check: bool = False,
                 **options) -> subprocess.CompletedProcess:
        """A command under the flavour's environment, in the build directory unless told otherwise.
        **Every run is from the build directory**, because the harness's `--resources` defaults to
        `./resources`, and the tests that read game data resolve it the way the harness does."""
        return subprocess.run(resolved(command, self.env), cwd=cwd or self.dir, env=self.env, check=check,
                              **options)
