"""`omw [flavour] integration [script args]`: upstream's integration tests (`scripts/integration_tests.py`) on the
example suite at the revision its GitLab pipeline pins, against this build's `openmw`, over whichever backend the
environment names, in a window of SDL's offscreen driver, which needs no display. The suite is GitLab's archive of
the revision, which carries its LFS files whole, fetched once beside a stamp naming it."""

import shutil
import sys
import tarfile
from pathlib import Path

from omw.build import Build
from omw.fetch import build_beside, download
from omw.system import ROOT, read_text, status

# The revision upstream's GitLab pipeline pins, which `upstream.yml` copies at each merge, so a merge
# that moves the suite and the tests together moves this run with them.
EXAMPLE_SUITE_REVISION = "a1ba6fc9d7315c2158ef4c60349fc4e71b7ca343"
_ARCHIVE = "https://gitlab.com/OpenMW/example-suite/-/archive/{revision}/example-suite-{revision}.tar.gz"

# The suite folders whose `data` the pipeline hands the tests, in its order.
_DATA = ("game_template", "example_animated_creature", "the_hub", "integration_tests", "example_static_props")


def fetch_suite(build: Build, revision: str) -> Path:
    """The suite at `revision` in the build directory, fetched where the stamp names another or none."""
    folder = build.dir / "example-suite"
    stamp = folder / ".revision"
    if stamp.is_file() and read_text(stamp).strip() == revision:
        return folder

    archive = build.dir / f"example-suite-{revision}.tar.gz"
    download(_ARCHIVE.format(revision=revision), archive)

    def unpack(into: Path) -> None:
        with tarfile.open(archive) as tar:
            tar.extractall(into, filter="data")
        (into / f"example-suite-{revision}" / ".revision").write_text(revision + "\n")

    shutil.rmtree(folder, ignore_errors=True)
    build_beside(folder, unpack, within=f"example-suite-{revision}")
    archive.unlink()
    return folder


def script_line(openmw: Path, workdir: Path, suite: Path, args: list[str]) -> list[str | Path]:
    """`scripts/integration_tests.py` over the suite as upstream's pipeline runs it: the suite's own
    settings, and the `data` folder of each of its parts, in the pipeline's order."""
    data = [argument for name in _DATA for argument in ("--data", suite / name / "data")]
    return [
        sys.executable,
        ROOT / "scripts" / "integration_tests.py",
        "--omw", openmw,
        "--workdir", workdir,
        "--config", suite,
        *data,
        *args,
        ROOT / "scripts" / "data" / "integration_tests",
    ]


def integration(build: Build, args: list[str]) -> int:
    """Builds `openmw`, fetches the suite, and runs the tests, `args` to the script. **No crash dialog and no fatal
    error box**, which would wait for a click nobody makes."""
    build.build(["openmw"])
    suite = fetch_suite(build, EXAMPLE_SUITE_REVISION)
    command = script_line(build.binary("openmw"), build.dir / "integration_tests_output", suite, args)
    env = dict(build.env, OPENMW_CRASH_DIALOG="0", SDL_VIDEO_DRIVER="offscreen")
    return status(build.run_here(command, env=env).returncode)
