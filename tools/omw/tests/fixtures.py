"""What the tests of several modules stand up alike."""

import shutil
import tempfile
import unittest
from pathlib import Path

from omw.build import Build


def temporary_build(case: unittest.TestCase, flavour: str = "package") -> Build:
    """A build of `flavour` whose directory is a folder of the test's own, removed after it."""
    build = Build(flavour)
    build.dir = Path(tempfile.mkdtemp())
    case.addCleanup(shutil.rmtree, build.dir)
    return build


def write_cache(build: Build, *lines: str) -> None:
    """`build`'s CMake cache, holding `lines` and nothing else."""
    (build.dir / "CMakeCache.txt").write_text("".join(line + "\n" for line in lines))
