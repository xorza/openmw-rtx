"""`omw [flavour] integration [script args]`: upstream's integration tests — the Lua and game-logic
suites its GitLab pipeline runs (`scripts/integration_tests.py`) — on the example suite at the
revision that pipeline pins, against this build's `openmw`.

**Over whichever backend the environment names, in a window of SDL's offscreen driver**: the
device's on a box with a card, which is what reads the ray tracer's whole effect on the game, and on
CI's runners, which have none, the null backend (`OPENMW_RTX_BACKEND=null`). The offscreen driver
needs no display, so the game opens no window on a desk and runs on a box with no monitor; the
device then traces every frame and presents none (`MWRender::RtxWindow::hasDisplay`).

**The suite as GitLab's archive of the pinned revision**, which carries its LFS files whole, so no
`git-lfs` is needed: fetched once into the build directory beside a stamp naming the revision, and
again only when the revision moves."""

import re
import shutil
import sys
import tarfile
from pathlib import Path

from omw.build import Build
from omw.fetch import build_beside, download
from omw.system import ROOT, Refusal, read_text, status

_PIPELINE = ROOT / ".gitlab-ci.yml"
_REVISION = re.compile(r"^ *EXAMPLE_SUITE_REVISION: *([0-9a-f]{40}) *$", re.MULTILINE)
_ARCHIVE = "https://gitlab.com/OpenMW/example-suite/-/archive/{revision}/example-suite-{revision}.tar.gz"

# The suite folders whose `data` the pipeline hands the tests, in its order.
_DATA = ("game_template", "example_animated_creature", "the_hub", "integration_tests", "example_static_props")


def suite_revision(pipeline: str) -> str:
    """The one example suite revision upstream's pipeline pins, read from its text, so a merge that
    moves the suite and the tests together moves this run with them."""
    revisions = sorted(set(_REVISION.findall(pipeline)))
    if len(revisions) != 1:
        raise Refusal(f".gitlab-ci.yml names no single example suite revision: {', '.join(revisions) or 'none'}")
    return revisions[0]


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
    """Builds `openmw`, fetches the suite, and runs the tests; `args` go to the script, such as
    `--test_filter=<pattern>`. **No crash dialog and no fatal error box**: either would wait for a
    click in a run nobody watches."""
    build.build(["openmw"])
    suite = fetch_suite(build, suite_revision(read_text(_PIPELINE)))
    command = script_line(build.binary("openmw"), build.dir / "integration_tests_output", suite, args)
    env = dict(build.env, OPENMW_CRASH_DIALOG="0", SDL_VIDEO_DRIVER="offscreen")
    return status(build.run_here(command, env=env).returncode)
