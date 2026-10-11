"""`omw gate`: everything a change owes before it is called done, in order of cost, stopping at the
first failure — so a formatting slip is found in seconds and not after the walk.

**In the full flavour**, every program the tree has, as CI's Linux leg builds and tests them: a CS
or a wizard the gate never compiled was a failure only CI found.

**Never a gate beside a build, or beside another gate**: the reading is then about the machine.
`check` writes its pictures where it always does, under `check/` in the build."""

import unittest
from collections.abc import Callable

from omw import formatting, lint, listing, spellings, testing
from omw.build import Build
from omw.repeat import repeat
from omw.system import ROOT, Refusal


def self_test() -> int:
    """The driver's own tests, which take a second."""
    tools = ROOT / "tools"
    suite = unittest.defaultTestLoader.discover(str(tools / "omw" / "tests"), top_level_dir=str(tools))
    return 0 if unittest.TextTestRunner(verbosity=0).run(suite).wasSuccessful() else 1


def _build(build: Build) -> int:
    build.build(build.default_targets + build.test_targets())
    return 0


def _release_compile(build: Build) -> int:
    """The release build too: without its asserts, the optimizer proves paths the debug build never
    shows a warning on, such as a lookup that can now reach a null. **Every program the preset
    configures**, as CI's `full` builds every program the tree has: a tool only CI linked, such as
    `openmw-rtx-spirv-digest`, was a link error the gate passed and every CI platform failed."""
    Build("release").build(["all"])
    return 0


# **The steps, in the order `gate` runs them**: each a name, which `omw help` prints, and what it
# runs on the gate's build, nought where it passed. A build that fails raises before it answers.
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
