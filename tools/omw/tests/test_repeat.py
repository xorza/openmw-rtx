import contextlib
import io
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw.build import Build
from omw.repeat import DIFFERED_STATUS, repeat
from omw.system import ROOT, Refusal, read_text

# A harness that passes and leaves a child holding its output for 0.3 s, as the crash monitor does.
OUTLIVED = ("import subprocess, sys; "
            "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(0.3)'], stdout=sys.stdout); "
            "print('identical')")


class OutlivedHarness:
    def harness(self, verb: str, *args, **options) -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, "-c", OUTLIVED], check=False, **options)


class StatusHarness:
    """A harness whose first run passes and whose every later run ends with `status`, saying nothing
    the report would."""

    def __init__(self, status: int):
        self.status = status
        self.runs = 0

    def harness(self, verb: str, *args, **options) -> subprocess.CompletedProcess:
        code = 0 if self.runs == 0 else self.status
        self.runs += 1
        return subprocess.CompletedProcess(args, code, stdout=b"a report in other words\n")


class RepeatTest(unittest.TestCase):
    def test_a_pair_is_judged_by_its_status_and_not_by_its_report(self):
        for status, judged, says in ((DIFFERED_STATUS, 1, "NOT repeatable"), (1, 1, "the run itself failed")):
            with self.subTest(status=status):
                out = Path(tempfile.mkdtemp(prefix="omw-repeat-test-"))
                self.addCleanup(shutil.rmtree, out, ignore_errors=True)
                errors = io.StringIO()
                with (mock.patch("omw.repeat.tempfile.mkdtemp", return_value=str(out)),
                      contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors)):
                    self.assertEqual(repeat(StatusHarness(status), []), judged)
                self.assertIn(says, errors.getvalue())

    def test_a_pair_that_agreed_leaves_no_folder_though_a_child_outlived_the_run(self):
        out = Path(tempfile.mkdtemp(prefix="omw-repeat-test-"))
        self.addCleanup(shutil.rmtree, out, ignore_errors=True)
        with (mock.patch("omw.repeat.tempfile.mkdtemp", return_value=str(out)),
              contextlib.redirect_stdout(io.StringIO())):
            self.assertEqual(repeat(OutlivedHarness(), []), 0)
        self.assertFalse(out.exists())

    def test_the_differed_status_is_the_harness_own(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "model" / "benchrun.hpp")
        stated = re.search(r"inline constexpr int sDifferedStatus = (\d+);", text)
        self.assertIsNotNone(stated)
        self.assertEqual(int(stated.group(1)), DIFFERED_STATUS)
        self.assertNotIn(DIFFERED_STATUS, (0, 1), "a run that differed must not read as passed or as failed")

    def test_a_line_that_moves_the_walk_is_refused_before_anything_builds(self):
        walk = "repeat walks `one-cell-walk` for six seconds, always, and {} would move it"
        cases = [
            (["--views=seyda-neen-ship"], walk.format("--views=seyda-neen-ship")),
            (["--suite", "bounce"], walk.format("--suite")),
            (["--seconds=2"], walk.format("--seconds=2")),
            (["--frames=60"], walk.format("--frames=60")),
            (["--pairs=0"], "--pairs=0 is not a count of one or more"),
            (["--upscale=quality"], "repeat sets --upscale itself, on every run"),
            (["--hold"], "repeat sets --hold itself, on every run"),
            (["--against=old"], "repeat sets --against itself, on every run"),
            (["--out=mine"], "repeat sets --out itself, on every run"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                with self.assertRaises(Refusal) as refused:
                    repeat(Build("debug"), args)
                self.assertEqual(str(refused.exception), message)


if __name__ == "__main__":
    unittest.main()
