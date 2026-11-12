"""A flavour's build directory: configured from its preset, built, and asked what it holds.

**How each flavour is configured is `CMakePresets.json`'s**, in the form CMake and every IDE read: a
flavour on a system is the preset `<flavour>-<system>`, and the reasons stand beside each variable
there. What this adds is what a preset cannot say — which system it is on and what that system
lacks, when a directory must be configured again, and what each flavour builds by default."""

import contextlib
import json
import os
import subprocess
import sys
import time
from collections.abc import Iterator
from pathlib import Path

from omw import deps, presets
from omw.system import (
    EXE,
    ROOT,
    SYSTEM,
    WINDOWS,
    Refusal,
    environment_key,
    exclusive,
    jobs,
    msvc_environment,
    output,
    read_text,
    resolved,
    run,
)

FLAVOURS = ("debug", "release", "asan", "tsan")

# The flavours under a sanitizer, which MSVC's has not been measured for on this tree.
SANITIZED = ("asan", "tsan")

# The file a directory keeps the digest it was last configured from in.
CONFIGURED_FROM = "omw-preset.sha256"
# What a run holds while it writes a build directory (`Build._building`).
BUILD_LOCK = "omw-build.lock"

# 2000-01-02 UTC, the time `build.ninja` takes where it was dated after the clock: before every input
# a checkout has, and a day past 2000, the epoch Ninja counts its times from on Windows, so the time
# is not negative there.
MANIFEST_AGED = 946_771_200.0


def manifest_inputs(query: str, directory: Path) -> list[Path]:
    """The inputs `ninja -t query build.ninja` names, explicit, implicit and order-only alike: the
    lines under `input:`, deeper than the headings, with their `|` or `||` taken off. A relative one
    is from the build `directory`."""
    inputs: list[Path] = []
    within = False
    for line in query.splitlines():
        if line.startswith("  ") and not line.startswith("    "):
            within = line.strip().startswith("input:")
        elif within and line.startswith("    "):
            inputs.append(directory / line.strip().removeprefix("||").removeprefix("|").strip())
    return inputs


def deps_inputs(listing: str, directory: Path) -> list[Path]:
    """The files `ninja -t deps` names, each once: the headers each compile said it read, indented
    under the line of the output that read them. A relative one is from the build `directory`."""
    named = dict.fromkeys(line.strip() for line in listing.splitlines() if line.startswith("    "))
    return [directory / path for path in named]


def ninja_outputs(listing: str, directory: Path) -> list[Path]:
    """The files `ninja -t targets all` names, a line `<output>: <rule>` each, but the phony targets,
    which name no file. A relative one is from the build `directory`."""
    named = (line.rsplit(": ", 1) for line in listing.splitlines() if ": " in line)
    return [directory / path for path, rule in named if rule != "phony"]


def dated_after(paths: list[Path], now: float) -> list[Path]:
    """The files of `paths` dated after `now`, each once: Ninja names a file CMake spelled both ways
    once relative and once whole."""
    ahead: dict[Path, Path] = {}
    for path in paths:
        if path.is_file() and path.stat().st_mtime > now:
            ahead.setdefault(path.resolve(), path)
    return list(ahead.values())


def redate_ahead(paths: list[Path], now: float, root: Path = ROOT) -> list[Path]:
    """The files of `paths` dated after `now`, each given the time `now`. **Only inside `root`**: one
    outside it is not the driver's to touch, and is refused before any file is touched."""
    ahead = dated_after(paths, now)
    outside = next((path for path in ahead if not path.resolve().is_relative_to(root.resolve())), None)
    if outside is not None:
        raise Refusal(f"{outside} is dated after the clock, and the build reads it, so whatever is built from it "
                      "is older than it and built again on every run: give it the time now")
    for path in ahead:
        os.utime(path, (now, now))
    return ahead


def remove_ahead(paths: list[Path], now: float, directory: Path) -> list[Path]:
    """The files of `paths` inside the build `directory` dated after `now`, each removed, so Ninja
    builds it again, but `build.ninja`, which takes `MANIFEST_AGED`: Ninja regenerates it from the
    cache for that, and CMake writes it at every regeneration, where a removed one left the
    directory to be configured afresh, the cache dropped and every compiler found again. **Only
    inside it**: an output elsewhere, a translation Qt's rule writes into the tree, is source as
    well."""
    within = directory.resolve()
    inside = [path for path in dated_after(paths, now) if path.resolve().is_relative_to(within)]
    for path in inside:
        if path.resolve() == within / "build.ninja":
            os.utime(path, (MANIFEST_AGED, MANIFEST_AGED))
        else:
            path.unlink()
    return inside


def configured_from(directory: Path, digest: str) -> bool:
    """Whether `directory` stands configured from the preset `digest` names, so a build there need
    only bring what CMake wrote up to date: the stamp of a configure that succeeded from that digest,
    and both halves of what the configure left — the cache the regeneration reads and the Ninja file
    it writes. A directory missing either is configured again, whatever its stamp says."""
    stamp = directory / CONFIGURED_FROM
    return (stamp.is_file() and read_text(stamp).strip() == digest
            and (directory / "CMakeCache.txt").is_file() and (directory / "build.ninja").is_file())


def properties_of(test: dict) -> dict:
    """A test of CTest's `--show-only=json-v1` listing, its properties by name."""
    return {property["name"]: property["value"] for property in test.get("properties", [])}


def targets_of(tests: list[dict]) -> dict[str, bool]:
    """Each target CTest's tests name (`OPENMW_TARGET`), and whether every test of it needs a device."""
    found: dict[str, bool] = {}
    for test in tests:
        properties = properties_of(test)
        if "OPENMW_TARGET" in properties:
            target = properties["OPENMW_TARGET"]
            found[target] = found.get(target, True) and "device" in properties.get("LABELS", [])
    return found


def device_free(tests: list[dict]) -> list[str]:
    """The targets of `tests` that run without a device: those with a test not labelled `device`."""
    return sorted(target for target, device in targets_of(tests).items() if not device)


# **The flags CMake reads from the environment** into a language's `_INIT` flags at a first configure: none reaches a
# build, whose every flag is `cmake/Flags.cmake`'s, as a desk's `LDFLAGS=-fuse-ld=mold` named a second linker
# before lld on every link.
ENVIRONMENT_FLAGS = ("CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "ASMFLAGS")


class Build:
    def __init__(self, flavour: str):
        if flavour not in FLAVOURS:
            raise Refusal(f"no flavour is called {flavour!r}: {', '.join(FLAVOURS)}")
        if flavour in SANITIZED and WINDOWS:
            raise Refusal(f"{flavour} is Linux's here: MSVC's sanitizers have not been measured on this tree")
        self.flavour = flavour
        self.dir = ROOT / f"build-{flavour}"
        self.preset = f"{flavour}-{SYSTEM}"
        self._env: dict[str, str] | None = None
        self._configured = False
        self._built: set[str] = set()
        self._holds_directory = False
        self._tests: list[dict] | None = None

    @property
    def default_targets(self) -> list[str]:
        """What `build` builds when the line names nothing, beside the tests the build has: every program,
        which every flavour configures."""
        return ["all"]

    @property
    def env(self) -> dict[str, str]:
        """The environment every command of this flavour runs under: the vcpkg set and the Vulkan loader
        (`deps.vulkan_environment`), the pinned CMake, MSVC's or the pinned Clang (`deps.compiler_environment`),
        none of the environment's flags (`ENVIRONMENT_FLAGS`), and the test preset's own — the sanitizers'
        options, without which the asan build has no device and a race in the tsan build does not fail the run."""
        if self._env is None:
            if WINDOWS:
                env = msvc_environment()
            else:
                env = dict(os.environ)
                deps.compiler_environment(env)
            deps.vulkan_environment(env)
            deps.cmake_environment(env)
            for name in ENVIRONMENT_FLAGS:
                env.pop(name, None)
            if presets.has_test_preset(self.preset):
                env = presets.test_environment(self.preset, env)
            self._env = env
        return self._env

    @property
    def ninja(self) -> str:
        """**The pinned Ninja, which the driver runs on a build's files itself**, and never the one a directory's
        cache names: a directory configured with an older one keeps naming it until it is configured again, and
        an old Ninja refuses the files a new CMake writes."""
        return self.env[environment_key("OMW_NINJA")]

    def cached(self, name: str) -> str:
        """**The value the build's CMake cache holds for `name`**, refused where it holds none: no cache
        yet, no such entry, an empty one, or a `-NOTFOUND`, which CMake reads as false: what CMake found,
        such as a program or a library."""
        cache = self.dir / "CMakeCache.txt"
        for entry in read_text(cache).splitlines() if cache.is_file() else []:
            key, _, value = entry.partition("=")
            if key.split(":", 1)[0] == name and value and not value.endswith("-NOTFOUND"):
                return value
        raise Refusal(f"the {self.flavour} build's cache names no {name}: configure it again")

    def folder(self, name: str) -> Path:
        """**A folder the build names for the driver**, out of the `omw-folders.json` its configure
        writes: the shaders and what the tests write, which the top-level `CMakeLists.txt` and
        `cmake/Tests.cmake` name once. Refused where the file or the name is missing."""
        listed = self.dir / "omw-folders.json"
        folders = json.loads(read_text(listed)) if listed.is_file() else {}
        if not folders.get(name):
            raise Refusal(f"the {self.flavour} build names no {name} in {listed.name}: configure it again")
        return Path(folders[name])

    @contextlib.contextmanager
    def _building(self) -> Iterator[None]:
        """**One process at a time writes a build directory.** A gate regenerating `build.ninja` beside
        a window's build reported `volk::volk_headers` missing from a tree nothing was wrong with, most
        likely as the two took CMake's `pkgRedirects` from under each other. Held by this
        run for as long as it writes, and not while it runs what it built, so a window stays open
        beside a build; once within a run, which `build` and the `configure` inside it both ask."""
        if self._holds_directory:
            yield
            return
        with exclusive(self.dir / BUILD_LOCK, f"building {self.dir.name}"):
            self._holds_directory = True
            try:
                yield
            finally:
                self._holds_directory = False

    def configure(self, stdout=None) -> None:
        """**Configured again whenever what its preset expands from changes, and from nothing
        else.** A directory configured once and never again keeps the cache it was first given, so a
        preset edited later never reached it: `build-release` went on building the tests its preset
        had since turned off. **The stamp says a configure succeeded, and nothing more**: removed as
        one starts and written once it has succeeded, so one that failed or was stopped is tried
        again rather than handed to Ninja — a fresh configure drops the cache first, and an old
        stamp beside no cache sent the build to a regeneration with nothing to regenerate from.

        **Fresh, and not the preset laid over the old cache**: a variable a preset stops naming would
        otherwise keep the value it last set.

        **Neither `--clean-first` nor `ninja -t cleandead`**: files/lang/*.ts are source that a Qt
        translation rule writes into the tree, so Ninja logs them as outputs, and both delete them.

        **Once a run**: what the presets expand from does not move while the driver runs, and a
        cache edited since is Ninja's to notice, since `build.ninja` depends on it."""
        if self._configured:
            return
        with self._building():
            self._configure(stdout)

    def _configure(self, stdout) -> None:
        self._remove_outputs_ahead()
        env = self.env
        # The set's digest beside the presets': its path no longer moves with it (`deps.held_set_dir`), and a cache
        # configured from another set would keep the paths of libraries the new one names otherwise.
        digest = f"{presets.digest(env)} {deps.set_digest()}"
        stamp = self.dir / CONFIGURED_FROM
        if configured_from(self.dir, digest):
            self._redate_manifest_inputs()
            # What CMake wrote is brought up to date with the CMake files as Ninja would before a
            # build, so what is asked of it before one — the tests it has — is not a stale answer.
            run(["cmake", "--build", "--preset", self.preset, "--target", "build.ninja"], env=env, cwd=ROOT,
                stdout=subprocess.DEVNULL)
            self._configured = True
            return

        deps.vcpkg_set()
        if WINDOWS:
            deps.vulkan_loader()

        stamp.unlink(missing_ok=True)
        run(["cmake", "-S", ROOT, "--preset", self.preset, "--fresh"], env=env, stdout=stdout)
        self._redate_manifest_inputs()


        stamp.write_text(digest + "\n")
        self._configured = True

    def _redate_manifest_inputs(self) -> None:
        """**Nothing `build.ninja` is made from dated after the clock.** A checkout written while the
        clock ran ahead, set right since, dates its CMake files later than any `build.ninja` CMake
        writes from them, so Ninja writes it again on every try and gives up after a hundred, naming
        neither the file nor the clock. Each such input is given the time now, which changes nothing
        it holds."""
        query = output([self.ninja, "-C", self.dir, "-t", "query", "build.ninja"], env=self.env)
        ahead = redate_ahead(manifest_inputs(query, self.dir), time.time())
        if ahead:
            print(f"omw: {len(ahead)} files build.ninja is made from were dated after the clock, {ahead[0]} among "
                  "them; they carry the time now", file=sys.stderr)

    def _remove_outputs_ahead(self) -> None:
        """**Nothing Ninja built dated after the clock.** An output built while the clock ran ahead,
        set right since, is newer than every edit made before the clock passes it again, so no such
        edit reaches the build. Removed and not given the time now: a command CMake marks `restat` is
        judged by the time Ninja logged, which a file's own time does not move, and an output and an
        input both given one time read as built from what the input holds. One outside the build
        directory is source as well, and takes the time now. Before the configure, which brings
        `build.ninja` up to date. **Only where a configure succeeded**, which its stamp says: one that
        failed leaves a `build.ninja` whose rules it never wrote, which Ninja refuses to list, and the
        configure that follows starts afresh."""
        if not ((self.dir / CONFIGURED_FROM).is_file() and (self.dir / "build.ninja").is_file()
                and (self.dir / "CMakeCache.txt").is_file()):
            return
        listing = output([self.ninja, "-C", self.dir, "-t", "targets", "all"], env=self.env)
        outputs = ninja_outputs(listing, self.dir)
        now = time.time()
        removed = remove_ahead(outputs, now, self.dir)
        redated = redate_ahead(outputs, now)
        if removed or redated:
            print(f"omw: {len(removed) + len(redated)} outputs were dated after the clock, "
                  f"{(removed or redated)[0]} among them; {len(removed)} are built again", file=sys.stderr)

    def _redate_inputs_ahead(self, targets: list[str]) -> None:
        """**Nothing a build reads dated after the clock**: a checkout written while the clock ran
        ahead, set right since, leaves files newer than whatever is built from them now, so every
        build builds it again until the clock passes them. What `targets` read is the inputs their
        manifest names and the headers the deps log holds, read whole, since it names them under each
        object and not under a target."""
        ninja = self.ninja
        listed = output([ninja, "-C", self.dir, "-t", "inputs", *targets], env=self.env)
        logged = output([ninja, "-C", self.dir, "-t", "deps"], env=self.env)
        inputs = [self.dir / line for line in listed.splitlines() if line] + deps_inputs(logged, self.dir)
        ahead = redate_ahead(inputs, time.time())
        if ahead:
            print(f"omw: {len(ahead)} files the build reads were dated after the clock, {ahead[0]} among them; "
                  "they carry the time now", file=sys.stderr)

    def build(self, targets: list[str], stdout=None) -> None:
        """`-k 0` keeps Ninja going past a failed object, so one run of a build that breaks names
        every object that broke — on a compiler this box does not have, that is the difference
        between one round trip through CI and five.

        **Each target once a run**: nothing a run does after its first build writes a source, and
        Ninja read the whole graph again to find a target it had just built up to date — the gate
        built the harness and every test binary twice, and `omw game` ran Ninja twice."""
        wanted = [target for target in targets if target not in self._built]
        if not wanted:
            return
        with self._building():
            self.configure(stdout)
            self._redate_inputs_ahead(wanted)
            # Through the flavour's build preset, which CMake reads from the source tree alone.
            run(["cmake", "--build", "--preset", self.preset, f"-j{jobs()}", "--target", *wanted, "--", "-k", "0"],
                env=self.env, cwd=ROOT, stdout=stdout)
        self._built.update(wanted)

    def build_default(self, without_device: bool = False) -> None:
        """What `omw build` and the gate build where nothing is named: `default_targets` and the tests
        the build has, those a box with no driver runs where `without_device` says so."""
        self.build(self.default_targets + self.test_targets(without_device))

    def tests(self) -> list[dict]:
        """**The tests this build has, asked of its own configuration** through CTest, and not of a
        binary on disk: a binary on disk is only what some earlier build left there. Asked once a
        run, which configures once."""
        if self._tests is None:
            self.configure()
            listing = output(["ctest", "--test-dir", self.dir, "--show-only=json-v1"], env=self.env)
            self._tests = json.loads(listing).get("tests", [])
        return self._tests

    def test_targets(self, without_device: bool = False) -> list[str]:
        """The targets the tests run, which `cmake/Tests.cmake` names on each: a test whose binary is
        not built yet has no command in CTest's listing. `without_device` leaves out a target whose
        every test is labelled `device`, which a box with no driver never runs."""
        return device_free(self.tests()) if without_device else sorted(targets_of(self.tests()))

    def binary(self, name: str) -> Path:
        return self.dir / f"{name}{EXE}"

    def harness_line(self, verb: str, *args: str | Path) -> list[str | Path]:
        """`openmw-rtxtool <verb> [args]`, for a caller that runs it under another program — perf —
        with the binary built first, once a run."""
        self.build_harness()
        return [self.binary("openmw-rtxtool"), verb, *args]

    def build_harness(self) -> None:
        """`openmw-rtxtool`: asked first by a caller that makes a folder for the harness's output, so
        a build that fails leaves no folder behind."""
        self.build(["openmw-rtxtool"])

    def harness(self, verb: str, *args: str | Path, **options) -> subprocess.CompletedProcess:
        """The harness's `verb`, from the build directory: the one way the driver starts it."""
        return self.run_here(self.harness_line(verb, *args), **options)

    def run_here(self, command: list, cwd: Path | None = None, check: bool = False,
                 env: dict[str, str] | None = None, **options) -> subprocess.CompletedProcess:
        """A command under the flavour's environment, or under `env`, a copy of it with more in it, in
        the build directory unless told otherwise. **Every run is from the build directory**, because
        the harness's `--resources` defaults to `./resources`, and the tests that read game data
        resolve it the way the harness does."""
        cwd = cwd or self.dir
        env = self.env if env is None else env
        return subprocess.run(resolved(command, env, cwd), cwd=cwd, env=env, check=check, **options)
