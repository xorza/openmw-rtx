"""`omw [flavour] gate`: everything a change owes before it is called done, in order of cost, stopping
at the first failure — so a formatting slip is found in seconds and not after the walk.

**Never a gate beside a build, or beside another gate**: the reading is then about the machine.
`check` writes its pictures where it always does, under `check/` in the build."""

import unittest

from omw import formatting, listing, presets, spellings, testing
from omw.build import Build
from omw.repeat import repeat
from omw.system import ROOT, Refusal

# **The steps, in the order `gate` runs them**: the one statement of it, which `omw help` prints and
# AGENTS.md points at.
STEPS = ("format check, spellings, the driver's tests, build, the listing check, the release compile, "
         "tests and their timing, check, repeat")


def self_test() -> bool:
    """The driver's own tests, which take a second."""
    tools = ROOT / "tools"
    suite = unittest.defaultTestLoader.discover(str(tools / "omw" / "tests"), top_level_dir=str(tools))
    return unittest.TextTestRunner(verbosity=0).run(suite).wasSuccessful()


def gate(build: Build, args: list[str]) -> int:
    if args:
        raise Refusal("gate takes no arguments")
    # Before anything builds: a gate that ran no test would end `clean` on nothing.
    if not presets.has_test_preset(build.preset):
        raise Refusal(f"the {build.flavour} build has no tests, and a gate is its tests: `omw debug gate` runs them")
    if formatting.format_tree(["--check"]) != 0:
        return 1
    if spellings.check() != 0:
        return 1
    if not self_test():
        return 1

    targets = build.test_targets()
    build.build(build.default_targets + targets)
    if listing.check(build) != 0:
        return 1

    # The release build too: without its asserts, the optimizer proves paths the debug build never
    # shows a warning on, such as a lookup that can now reach a null. **Every program the preset
    # configures**, as CI's `full` builds every program the tree has: a tool only CI linked, such as
    # `openmw-rtx-spirv-digest`, was a link error the gate passed and every CI platform failed.
    release = Build("release")
    release.build(["all"])

    if testing.test(build, []) != 0:
        return 1
    if testing.timing(build) != 0:
        return 1

    if build.harness("check").returncode != 0:
        return 1
    if repeat(build, ["--pairs=1"]) != 0:
        return 1
    print("gate: clean")
    return 0
