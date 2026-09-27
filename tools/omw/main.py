"""The grammar of `omw`, and the one place a verb is sent to what does it."""

import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass

from omw import crash, deps, formatting, game, gate, kernels, package, perf, repeat, testing
from omw.build import FLAVOURS, Build
from omw.system import Refusal

USAGE = """\
omw [flavour] <verb> [args]: one grammar for every build, on the desk and in CI, on Linux and Windows.

  build [targets]              configure where the presets changed, then build the harness, the game
                               and the tests the build has, or the targets named
  test [--without-device] [--all] [ctest args]
                               the fork's suites through CTest, the crash matrix and the GPU binary
                               among them; `--without-device` leaves the GPU binary out, `--all` adds
                               upstream's, which the plain flavour always runs
  test <binary> [gtest args]   one test binary, built and run alone, for a filter
  game [args]                  openmw on the newest quicksave
  setup <morrowind dir>        write the openmw.cfg a fresh box has none of
  repeat [--pairs=N] [bench args]
                               two runs of one binary walk one place and must agree
  kernels [--against=<file>]   one digest per shader and tuple of its constants; against an earlier
                               listing, which tuples moved
  gate                         format, the driver's tests, build, the release and no-DLSS compiles,
                               test, check, repeat — stops at the first failure
  exec <command> [args]        a command in the build directory, under the flavour's environment
  archive [name]               the release archive into dist/, with its symbols: the package flavour
  profile [args]               the harness's CPU side under perf: the release flavour
  info, scene, shot, view, bench, check, film [args]
                               openmw-rtxtool's own verbs, from the build directory

  crash <dump> [symbols]       a player's crash dump, every thread named and lined, against a
                               release's -symbols.zip or the newest in dist/; no flavour
  format                       clang-format 14 over the working tree; no flavour
  bootstrap                    the pinned Vulkan SDK and NGX into deps/; no flavour

  flavour   directory       what it is
  debug     build-debug     -O2 -g with every assert and the tests: the everyday build, and the default
  release   build-release   -O3 -DNDEBUG, line tables and frame pointers: the build a number is quoted from
  asan      build-asan      debug under AddressSanitizer and UndefinedBehaviorSanitizer; Linux only
  nodlss    build-nodlss    debug without the DLSS SDK: the other binary
  package   build-package   release with the launcher, the wizard and the importers, portable
  plain     build-plain     the tree without the ray tracer, as upstream builds it, with its suites whole
"""


# The harness's own verbs, `sNames` in `apps/rtxtool/verbs.cpp`, which a test holds this to: a word
# that is none of these and none of the driver's is refused before anything is configured or built.
HARNESS_VERBS = ("info", "scene", "shot", "view", "bench", "check", "film")


def _harness(build: Build, verb: str, args: list[str]) -> int:
    """**No `--validation` of the driver's own.** Each build's binary already defaults to its
    flavour's layers, and a level on the line reads as asked for."""
    build.build(["openmw-rtxtool"])
    return build.run_here([build.binary("openmw-rtxtool"), verb, *args]).returncode


def _exec(build: Build, args: list[str]) -> int:
    if not args:
        raise Refusal("exec runs a command: `omw [flavour] exec <command> [args]`")
    return build.run_here(args).returncode


def _build(build: Build, args: list[str]) -> int:
    build.build(args or build.default_targets + build.test_targets())
    return 0


@dataclass(frozen=True)
class Verb:
    run: Callable[[Build, list[str]], int]
    # The one flavour the verb is made of, where it has one.
    flavour: str | None = None


BUILD_VERBS = {
    "build": Verb(_build),
    "test": Verb(testing.test),
    "game": Verb(game.game),
    "setup": Verb(game.setup),
    "repeat": Verb(repeat.repeat),
    "kernels": Verb(kernels.kernels),
    "gate": Verb(gate.gate),
    "exec": Verb(_exec),
    "archive": Verb(package.archive, flavour="package"),
    "profile": Verb(perf.profile, flavour="release"),
}

def _bootstrap(args: list[str]) -> int:
    if args:
        raise Refusal("bootstrap takes no arguments")
    deps.bootstrap()
    return 0


# Verbs no build is configured for: reading a dump or checking the format needs none, and a verb
# that configured one first fetched a dependency set and started MSVC to read a file.
BUILDLESS_VERBS: dict[str, Callable[[list[str]], int]] = {
    "crash": crash.read_crash,
    "format": formatting.check_format,
    "bootstrap": _bootstrap,
}


@dataclass(frozen=True)
class Line:
    """A command line, parsed: the flavour, `None` for a verb that takes none, the verb, the rest."""
    flavour: str | None
    verb: str
    args: list[str]


def parse(argv: list[str]) -> Line:
    """**The flavour is a closed set of words**, so a first word that is one is the flavour and any
    other is the verb, and the flavour can be left out: debug, or the one flavour a verb is made of.
    **The verb stays before the switches.** The harness reads its verb off its first argument and
    takes a leading dash to mean nobody named one, so a switch put first once ran `view` instead."""
    flavour = argv[0] if argv and argv[0] in FLAVOURS else None
    rest = argv[1:] if flavour else argv
    if not rest or rest[0] in ("help", "-h", "--help"):
        raise Refusal(USAGE)
    verb, args = rest[0], rest[1:]
    if verb.startswith("-"):
        raise Refusal(f"name a verb before the switches: `omw {flavour or 'debug'} view {' '.join(rest)}`")

    if verb in BUILDLESS_VERBS:
        if flavour is not None:
            raise Refusal(f"{verb} is not made of a build, so it takes no flavour")
        return Line(None, verb, args)
    if verb not in BUILD_VERBS and verb not in HARNESS_VERBS:
        known = [*BUILD_VERBS, *HARNESS_VERBS, *BUILDLESS_VERBS]
        raise Refusal(f"no verb or flavour is called {verb!r}: {', '.join(known)}, or a flavour first")
    own = BUILD_VERBS[verb].flavour if verb in BUILD_VERBS else None
    if own is not None and flavour not in (None, own):
        raise Refusal(f"{verb} is made of the {own} flavour: `omw {verb}`")
    return Line(flavour or own or "debug", verb, args)


def dispatch(line: Line) -> int:
    if line.flavour is None:
        return BUILDLESS_VERBS[line.verb](line.args)
    build = Build(line.flavour)
    if line.verb in BUILD_VERBS:
        return BUILD_VERBS[line.verb].run(build, line.args)
    return _harness(build, line.verb, line.args)


def main(argv: list[str]) -> int:
    try:
        return dispatch(parse(argv))
    except Refusal as refusal:
        message = str(refusal)
        print(message if message == USAGE else f"omw: {message}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as failed:
        command = failed.cmd if isinstance(failed.cmd, str) else " ".join(str(part) for part in failed.cmd)
        print(f"omw: `{command}` exited with {failed.returncode}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
