"""A flavour's build directory: configured from its preset, built, and asked what it holds.

**How each flavour is configured is `CMakePresets.json`'s**, as `<flavour>-<system>`, with each variable's reason
beside it. This adds what a preset cannot say: the system and what it lacks, when to configure again, and what each
flavour builds by default."""

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

# What `build` builds where the line names nothing, beside the tests: every program, which every flavour configures.
DEFAULT_TARGETS = ("all",)

# The file a directory keeps the digest it was last configured from in.
CONFIGURED_FROM = "omw-preset.sha256"
# What a run holds while it writes a build directory (`Build._building`).
BUILD_LOCK = "omw-build.lock"

# 2000-01-02 UTC, the time `build.ninja` takes where it was dated after the clock: before every input a checkout has,
# and a day past the epoch Ninja counts from on Windows, so the time is not negative there.
MANIFEST_AGED = 946_771_200.0


def manifest_inputs(query: str, directory: Path) -> list[Path]:
    """The inputs `ninja -t query build.ninja` names, explicit, implicit and order-only alike: the lines under
    `input:` with their `|` or `||` taken off. A relative one is from the build `directory`."""
    inputs: list[Path] = []
    within = False
    for line in query.splitlines():
        if line.startswith("  ") and not line.startswith("    "):
            within = line.strip().startswith("input:")
        elif within and line.startswith("    "):
            inputs.append(directory / line.strip().removeprefix("||").removeprefix("|").strip())
    return inputs


def deps_inputs(listing: str, directory: Path) -> list[Path]:
    """The headers `ninja -t deps` names, each once: indented under the output that read them. A relative one is
    from the build `directory`."""
    named = dict.fromkeys(line.strip() for line in listing.splitlines() if line.startswith("    "))
    return [directory / path for path in named]


def ninja_outputs(listing: str, directory: Path) -> list[Path]:
    """The files `ninja -t targets all` names, a line `<output>: <rule>` each, but the phony targets. A relative one
    is from the build `directory`."""
    named = (line.rsplit(": ", 1) for line in listing.splitlines() if ": " in line)
    return [directory / path for path, rule in named if rule != "phony"]


def dated_after(paths: list[Path], now: float) -> list[Path]:
    """The files of `paths` dated after `now`, each once: Ninja names a file CMake spelled both ways once relative
    and once whole."""
    ahead: dict[Path, Path] = {}
    for path in paths:
        if path.is_file() and path.stat().st_mtime > now:
            ahead.setdefault(path.resolve(), path)
    return list(ahead.values())


def redate_ahead(paths: list[Path], now: float, root: Path = ROOT) -> list[Path]:
    """The files of `paths` dated after `now`, each given the time `now`. **Only inside `root`**: one outside it is
    not the driver's to touch, and is refused before any file is touched."""
    ahead = dated_after(paths, now)
    outside = next((path for path in ahead if not path.resolve().is_relative_to(root.resolve())), None)
    if outside is not None:
        raise Refusal(f"{outside} is dated after the clock, and the build reads it, so whatever is built from it "
                      "is older than it and built again on every run: give it the time now")
    for path in ahead:
        os.utime(path, (now, now))
    return ahead


def remove_ahead(paths: list[Path], now: float, directory: Path) -> list[Path]:
    """The files of `paths` inside the build `directory` dated after `now`, each removed so Ninja builds it again;
    `build.ninja` instead takes `MANIFEST_AGED`, since a removed one left the directory to be configured afresh.
    **Only inside it**: an output elsewhere, such as a translation Qt writes into the tree, is source as well."""
    within = directory.resolve()
    inside = [path for path in dated_after(paths, now) if path.resolve().is_relative_to(within)]
    for path in inside:
        if path.resolve() == within / "build.ninja":
            os.utime(path, (MANIFEST_AGED, MANIFEST_AGED))
        else:
            path.unlink()
    return inside


def configured_digest(directory: Path) -> str | None:
    """The preset digest `directory` was last configured from with success, or none: the stamp of a configure that
    succeeded, beside both halves of what it left — the cache and the Ninja file. A directory missing either has
    none, whatever its stamp says."""
    stamp = directory / CONFIGURED_FROM
    if not (stamp.is_file() and (directory / "CMakeCache.txt").is_file() and (directory / "build.ninja").is_file()):
        return None
    return read_text(stamp).strip()


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


# **The flags CMake reads from the environment** into a first configure's `_INIT` flags: none reaches a build, whose
# every flag is `cmake/Flags.cmake`'s, as a desk's `LDFLAGS=-fuse-ld=mold` named a second linker on every link.
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
    def env(self) -> dict[str, str]:
        """The environment every command of this flavour runs under: the set and the Vulkan loader, the pinned
        CMake, MSVC's or the pinned Clang, none of the environment's flags (`ENVIRONMENT_FLAGS`), and the test
        preset's own — the sanitizers' options among them."""
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
        """**The pinned Ninja, never the one a directory's cache names**: a directory configured with an older one
        keeps naming it, and an old Ninja refuses the files a new CMake writes."""
        return self.env[environment_key("OMW_NINJA")]

    def cached(self, name: str) -> str:
        """**The value the build's CMake cache holds for `name`**, refused where it holds none: no cache, no entry,
        an empty one, or a `-NOTFOUND`."""
        cache = self.dir / "CMakeCache.txt"
        for entry in read_text(cache).splitlines() if cache.is_file() else []:
            key, _, value = entry.partition("=")
            if key.split(":", 1)[0] == name and value and not value.endswith("-NOTFOUND"):
                return value
        raise Refusal(f"the {self.flavour} build's cache names no {name}: configure it again")

    def folder(self, name: str) -> Path:
        """**A folder the build names for the driver** in the `omw-folders.json` its configure writes, as the
        top-level `CMakeLists.txt` and `cmake/Tests.cmake` name them. Refused where the file or the name is
        missing."""
        listed = self.dir / "omw-folders.json"
        folders = json.loads(read_text(listed)) if listed.is_file() else {}
        if not folders.get(name):
            raise Refusal(f"the {self.flavour} build names no {name} in {listed.name}: configure it again")
        return Path(folders[name])

    @contextlib.contextmanager
    def _building(self) -> Iterator[None]:
        """**One process at a time writes a build directory**: two regenerating `build.ninja` at once took CMake's
        `pkgRedirects` from under each other. Held while this run writes, not while it runs what it built, and
        taken once within a run."""
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
        """**Configured again whenever what its preset expands from changes, and from nothing else**, once a run;
        fresh rather than laid over the old cache, which would keep a value a preset stopped naming. **Neither
        `--clean-first` nor `ninja -t cleandead`**: both delete the `.ts` files Qt's rule writes into the tree."""
        if self._configured:
            return
        with self._building():
            self._configure(stdout)

    def _configure(self, stdout) -> None:
        self._remove_outputs_ahead()
        env = self.env
        # The set's digest beside the presets': a cache configured from another set keeps its library paths.
        digest = f"{presets.digest(env)} {deps.set_digest()}"
        if configured_digest(self.dir) == digest:
            self._redate_manifest_inputs()
            # What CMake wrote brought up to date, so what is asked of it before a build is no stale answer.
            run(["cmake", "--build", "--preset", self.preset, "--target", "build.ninja"], env=env, cwd=ROOT,
                stdout=subprocess.DEVNULL)
            self._configured = True
            return

        deps.vcpkg_set()
        if WINDOWS:
            deps.vulkan_loader()

        # The stamp says a configure succeeded: removed as one starts and written once it has.
        stamp = self.dir / CONFIGURED_FROM
        stamp.unlink(missing_ok=True)
        run(["cmake", "-S", ROOT, "--preset", self.preset, "--fresh"], env=env, stdout=stdout)
        self._redate_manifest_inputs()
        stamp.write_text(digest + "\n", encoding="utf-8")
        self._configured = True

    def _redate_manifest_inputs(self) -> None:
        """**Nothing `build.ninja` is made from dated after the clock**: Ninja would write it again on every try and
        give up after a hundred, naming neither the file nor the clock. Each such input takes the time now."""
        query = output([self.ninja, "-C", self.dir, "-t", "query", "build.ninja"], env=self.env)
        ahead = redate_ahead(manifest_inputs(query, self.dir), time.time())
        if ahead:
            print(f"omw: {len(ahead)} files build.ninja is made from were dated after the clock, {ahead[0]} among "
                  "them; they carry the time now", file=sys.stderr)

    def _remove_outputs_ahead(self) -> None:
        """**Nothing Ninja built dated after the clock**, or no edit made before the clock passes it reaches the
        build. Removed rather than redated, since a `restat` command is judged by the time Ninja logged. **Only
        where a configure succeeded**: a failed one leaves a `build.ninja` Ninja refuses to list."""
        if configured_digest(self.dir) is None:
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
        """**Nothing a build reads dated after the clock**, or every build builds it again: the inputs the manifest
        names for `targets` and the headers the deps log holds, read whole."""
        ninja = self.ninja
        listed = output([ninja, "-C", self.dir, "-t", "inputs", *targets], env=self.env)
        logged = output([ninja, "-C", self.dir, "-t", "deps"], env=self.env)
        inputs = [self.dir / line for line in listed.splitlines() if line] + deps_inputs(logged, self.dir)
        ahead = redate_ahead(inputs, time.time())
        if ahead:
            print(f"omw: {len(ahead)} files the build reads were dated after the clock, {ahead[0]} among them; "
                  "they carry the time now", file=sys.stderr)

    def build(self, targets: list[str], stdout=None) -> None:
        """Each target once a run, as nothing a run does after its first build writes a source. `-k 0` keeps Ninja
        past a failed object, so one run names every object that broke."""
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
        """`DEFAULT_TARGETS` and the tests the build has, those a box with no driver runs where `without_device`
        says so."""
        self.build([*DEFAULT_TARGETS, *self.test_targets(without_device)])

    def tests(self) -> list[dict]:
        """**The tests this build has, asked of its configuration** through CTest and not of a binary some earlier
        build left on disk; once a run."""
        if self._tests is None:
            self.configure()
            listing = output(["ctest", "--test-dir", self.dir, "--show-only=json-v1"], env=self.env)
            self._tests = json.loads(listing).get("tests", [])
        return self._tests

    def test_targets(self, without_device: bool = False) -> list[str]:
        """The targets the tests run, which `cmake/Tests.cmake` names on each. `without_device` leaves out a target
        whose every test is labelled `device`."""
        return device_free(self.tests()) if without_device else sorted(targets_of(self.tests()))

    def binary(self, name: str) -> Path:
        return self.dir / f"{name}{EXE}"

    def harness_line(self, verb: str, *args: str | Path) -> list[str | Path]:
        """`openmw-rtxtool <verb> [args]` for a caller that runs it under another program, built first."""
        self.build_harness()
        return [self.binary("openmw-rtxtool"), verb, *args]

    def build_harness(self) -> None:
        """`openmw-rtxtool`, asked first by a caller that makes a folder for its output, so a failed build leaves
        no folder behind."""
        self.build(["openmw-rtxtool"])

    def harness(self, verb: str, *args: str | Path, **options) -> subprocess.CompletedProcess:
        """The harness's `verb`, from the build directory: the one way the driver starts it."""
        return self.run_here(self.harness_line(verb, *args), **options)

    def run_here(self, command: list, cwd: Path | None = None, check: bool = False,
                 env: dict[str, str] | None = None, **options) -> subprocess.CompletedProcess:
        """A command under the flavour's environment, or `env`, a copy of it with more in it. **From the build
        directory** unless told otherwise, since the harness's `--resources` defaults to `./resources`."""
        cwd = cwd or self.dir
        env = self.env if env is None else env
        return subprocess.run(resolved(command, env, cwd), cwd=cwd, env=env, check=check, **options)
