"""`omw [flavour] gate`: everything a change owes before it is called done, in order of cost, stopping
at the first failure — so a formatting slip is found in seconds and not after the walk.

**Never a gate beside a build, or beside another gate**: the reading is then about the machine.
`check` writes its pictures where it always does, under `check/` in the build."""

import unittest

from omw import formatting, testing
from omw.build import Build
from omw.repeat import repeat
from omw.system import ROOT, Refusal


def self_test() -> bool:
    """The driver's own tests, which take a second."""
    tools = ROOT / "tools"
    suite = unittest.defaultTestLoader.discover(str(tools / "omw" / "tests"), top_level_dir=str(tools))
    return unittest.TextTestRunner(verbosity=0).run(suite).wasSuccessful()


def gate(build: Build, args: list[str]) -> int:
    if args:
        raise Refusal("gate takes no arguments")
    if build.flavour == "plain":
        raise Refusal("the gate is the ray tracer's, and the plain build has none: `omw plain test`")
    if formatting.check_format([]) != 0:
        return 1
    if not self_test():
        return 1

    targets = build.test_targets()
    build.build(build.default_targets + targets)

    # **The fork's own targets, compiled the way a number is taken.** `-DNDEBUG` compiles every
    # assert out, and a diagnostic that fires only once the assert is gone — a lookup the optimizer
    # can now prove reaches a null — is one the debug build never sees. Every target the fork owns,
    # since the flags that make a warning an error reach each of them.
    Build("release").build(["openmw-rtx-all"])
    # **The backend without the SDK, compiled the way a machine without one compiles it.** The
    # backend alone, because that is the one library the SDK divides.
    Build("nodlss").build(["openmw-rtx-vulkan"])

    if targets:
        if testing.test(build, []) != 0:
            return 1
    else:
        print(f"tests: the {build.flavour} build has none — `omw debug gate` runs them")

    if build.run_here([build.binary("openmw-rtxtool"), "check"]).returncode != 0:
        return 1
    if repeat(build, ["--pairs=1"]) != 0:
        return 1
    print("gate: clean")
    return 0
