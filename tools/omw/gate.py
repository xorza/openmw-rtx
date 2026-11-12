"""`omw gate`: everything a change owes before it is called done, in order of cost, stopping at the first failure.
In the debug flavour, which builds every program as CI's legs do. **Never beside a build or another gate**, or the
reading is about the machine."""

import unittest
from collections.abc import Callable

from omw import formatting, integration, lint, listing, spellings, testing
from omw.build import Build
from omw.repeat import repeat
from omw.system import ROOT, Refusal


def self_test() -> int:
    """The driver's own tests, which take a second."""
    tools = ROOT / "tools"
    suite = unittest.defaultTestLoader.discover(str(tools / "omw" / "tests"), top_level_dir=str(tools))
    return 0 if unittest.TextTestRunner(verbosity=0).run(suite).wasSuccessful() else 1


def _build(build: Build) -> int:
    build.build_default()
    return 0


def _release_compile(build: Build) -> int:
    """The release build too, every program: without its asserts the optimizer warns on paths the debug build
    never shows, and a tool only CI linked was a link error only CI found."""
    Build("release").build(["all"])
    return 0


# **The steps, in the order `gate` runs them**: each a name, which `omw help` prints, and what it runs on the
# gate's build, nought where it passed.
STEPS: tuple[tuple[str, Callable[[Build], int]], ...] = (
    ("format check", lambda build: formatting.format_tree(["--check"])),
    ("spellings", lambda build: spellings.check()),
    ("lint (CI's other source checks)", lambda build: lint.lint([])),
    ("the driver's tests", lambda build: self_test()),
    ("build", _build),
    ("the listing check", listing.check),
    ("the release compile", _release_compile),
    ("tests", lambda build: testing.test(build, [])),
    ("their timing", testing.timing),
    ("the integration tests", lambda build: integration.integration(build, [])),
    ("check", lambda build: build.harness("check").returncode),
    ("repeat", lambda build: repeat(build, ["--pairs=1"])),
)
SUMMARY = ", ".join(name for name, _ in STEPS)


def gate(build: Build, args: list[str]) -> int:
    if args:
        raise Refusal("gate takes no arguments")
    for _, step in STEPS:
        if step(build) != 0:
            return 1
    print("gate: clean")
    return 0
