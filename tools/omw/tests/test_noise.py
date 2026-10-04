import re
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from typing import IO, cast

from omw.build import Build
from omw.noise import Figures, Leg, Plan, Side, ab, plan, read_report, wants_ab
from omw.system import ROOT, Refusal


class PlanTest(unittest.TestCase):
    def test_a_switch_is_its_two_sides_over_the_moving_legs(self):
        self.assertEqual(
            plan(["--ab=antifirefly", "--suite=bounce"]),
            Plan(sides=(Side("on", "--antifirefly=true"), Side("off", "--antifirefly=false")),
                 legs=(Leg("strafe 150", ("--strafe=150",)), Leg("walk 150", ("--walk=150",))),
                 out=None, rest=("--suite=bounce",)))

    def test_values_still_distances_and_the_out_are_the_drivers(self):
        self.assertEqual(
            plan(["--views=x", "--ab=bounce-reuse=own,temporal", "--still", "--walk=-80", "--out=/tmp/ab"]),
            Plan(sides=(Side("own", "--bounce-reuse=own"), Side("temporal", "--bounce-reuse=temporal")),
                 legs=(Leg("still", ()), Leg("strafe 150", ("--strafe=150",)), Leg("walk -80", ("--walk=-80",))),
                 out=Path("/tmp/ab"), rest=("--views=x",)))

    def test_a_line_that_names_no_pair_is_refused(self):
        cases = [
            (["--suite=bounce"], "noise: the following arguments are required: --ab"),
            (["--ab"], "noise: argument --ab: expected one argument"),
            (["--ab=reuse=own"], "--ab=reuse=own names 1 values of reuse, and an A/B is two"),
            (["--ab=reuse=a,b,c"], "--ab=reuse=a,b,c names 3 values of reuse, and an A/B is two"),
            (["--ab="], "--ab= names no switch: `--ab=antifirefly` or `--ab=bounce-reuse=own,temporal`"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                with self.assertRaises(Refusal) as refused:
                    plan(args)
                self.assertEqual(str(refused.exception), message)

    def test_only_a_line_with_an_ab_is_the_drivers(self):
        self.assertTrue(wants_ab(["--views=x", "--ab=antilag"]))
        self.assertTrue(wants_ab(["--ab", "antilag"]))
        self.assertFalse(wants_ab(["--views=x", "--strafe=150"]))


class ReportTest(unittest.TestCase):
    def test_a_place_line_is_read_and_the_rest_passed_over(self):
        text = (
            "[01:50:20.009 I] Ray tracing session: stop 124 of 335\n"
            "  balmora-mages-guild          noise: frame mean 1.24 p99 12, 13 averaged mean 3.59 p99 27 — as clean; "
            "bias: frame 2.10, 13 averaged 1.41\n"
            "  seyda-neen-pond              noise: frame mean 0.49 p99 3, 16 averaged mean 2.70 p99 21 — noisier; "
            "bias: frame 1.57, 16 averaged 0.53\n"
            "  every frame is as clean as 13 frames averaged\n")
        self.assertEqual(read_report(text), {
            "balmora-mages-guild": Figures(1.24, 12, 2.10),
            "seyda-neen-pond": Figures(0.49, 3, 1.57),
        })

    def test_the_line_read_is_the_one_the_harness_prints(self):
        text = (ROOT / "apps" / "rtxtool" / "compare.cpp").read_text()
        printed = re.search(r'"(  \{:<28\} noise: frame mean .*?)"\s*"(.*?)"', text, re.DOTALL)
        self.assertIsNotNone(printed, "judgeNoise no longer prints a place's line where this looks for it")
        line = (printed.group(1) + printed.group(2)).removesuffix("\\n").replace("{:<28}", f"{'some-place':<28}")
        # Past the place: the p99, the frames averaged and their p99, the verdict, the frames again.
        line = line.replace("{:.2f}", "1.25").replace("{}", "7", 3).replace("{}", "as clean", 1).replace("{}", "7")
        self.assertEqual(read_report(line), {"some-place": Figures(1.25, 7, 1.25)})


class _Harness:
    """A build whose harness prints one place's line and ends with `status`."""

    def __init__(self, status: int):
        self.status = status

    def harness(self, verb: str, *args: str, stdout: IO[str], **options) -> subprocess.CompletedProcess:
        stdout.write("  some-place                   noise: frame mean 1.00 p99 9, 13 averaged mean 2.00 p99 20 — "
                     "noisier; bias: frame 0.50, 13 averaged 0.40\n")
        return subprocess.CompletedProcess([verb, *args], self.status)


class AbTest(unittest.TestCase):
    def test_only_a_run_that_judged_its_places_is_read(self):
        for status, expected in ((0, 0), (1, 0), (2, 1), (-6, 1)):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as out:
                printed, refused = StringIO(), StringIO()
                with redirect_stdout(printed), redirect_stderr(refused):
                    code = ab(cast(Build, _Harness(status)), ["--ab=antifirefly", f"--out={out}"])
                self.assertEqual(code, expected)
                if expected == 0:
                    self.assertIn("  some-place                       1.00     1.00     9     9   0.50   0.50",
                                  printed.getvalue())
                else:
                    self.assertIn(f"the run failed with status {status}", refused.getvalue())


if __name__ == "__main__":
    unittest.main()
