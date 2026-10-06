import re
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from typing import IO, cast

from omw.build import Build
from omw.noise import Figures, Leg, Plan, Side, ab, plan, read_report, read_sides, table, wants_ab
from omw.system import ROOT, Refusal, read_text


class PlanTest(unittest.TestCase):
    def test_a_switch_is_its_two_sides_over_the_moving_legs(self):
        self.assertEqual(
            plan(["--ab=antifirefly", "--suite=bounce"]),
            Plan(sides=(Side("on", "--antifirefly=true"), Side("off", "--antifirefly=false")),
                 legs=(Leg("strafe 150", ("--strafe=150",)), Leg("walk 150", ("--walk=150",))),
                 out=None, rest=("--suite=bounce",)))

    def test_values_still_distances_and_the_out_are_the_drivers(self):
        self.assertEqual(
            plan(["--views=x", "--ab=bounce-reuse=own,temporal", "--still", "--walk=-80", "--out=/tmp/ab", "--cut=1",
                  "--cut=2"]),
            Plan(sides=(Side("own", "--bounce-reuse=own"), Side("temporal", "--bounce-reuse=temporal")),
                 legs=(Leg("still", ()), Leg("cut 1", ("--cut=1",)), Leg("cut 2", ("--cut=2",)),
                       Leg("strafe 150", ("--strafe=150",)), Leg("walk -80", ("--walk=-80",))),
                 out=Path("/tmp/ab"), rest=("--views=x",)))

    def test_a_distance_of_nought_leaves_its_leg_out(self):
        self.assertEqual(plan(["--ab=antilag", "--views=x", "--still", "--strafe=0", "--walk=0"]).legs,
                         (Leg("still", ()),))
        self.assertEqual(plan(["--ab=antilag", "--strafe=0"]).legs, (Leg("walk 150", ("--walk=150",)),))

    def test_a_line_that_names_no_pair_is_refused(self):
        cases = [
            (["--suite=bounce"], "noise: the following arguments are required: --ab"),
            (["--ab"], "noise: argument --ab: expected one argument"),
            (["--ab=reuse=own"], "--ab=reuse=own names 1 values of reuse, and an A/B is two"),
            (["--ab=reuse=a,b,c"], "--ab=reuse=a,b,c names 3 values of reuse, and an A/B is two"),
            (["--ab="], "--ab= names no switch: `--ab=antifirefly` or `--ab=bounce-reuse=own,temporal`"),
            (["--ab=antilag", "--cut=2", "--cut=0"], "--cut=0 is not a frame after the cut: one or more"),
            (["--ab=antilag", "--strafe=0", "--walk=0.0"],
             "no leg is left to run: name --still, --cut=N, or a distance that is not nought"),
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
            "  balmora-mages-guild          noise: frame mean 1.24 p99 12.35, 13 averaged mean 3.59 p99 27.00 — as clean; "
            "bias: frame 2.10, 13 averaged 1.41; fireflies 0.40 in a thousand\n"
            "  seyda-neen-pond              noise: frame mean 0.49 p99 3.00, 16 averaged mean 2.70 p99 21.00 — noisier; "
            "bias: frame 1.57, 16 averaged 0.53; fireflies 0.00 in a thousand\n"
            "  every frame is as clean as 13 frames averaged\n")
        self.assertEqual(read_report(text), {
            "balmora-mages-guild": Figures(1.24, 12.35, 2.10, 0.40),
            "seyda-neen-pond": Figures(0.49, 3.00, 1.57, 0.00),
        })

    def test_the_sides_part_where_the_harness_prints_its_versus_line(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "main.cpp")
        printed = re.search(r'out\(\) << std::format\("(versus --)\{\}', text)
        self.assertIsNotNone(printed, "commandNoise no longer prints the versus line where this looks for it")
        place = ("  some-place                   noise: frame mean {} p99 9.00, 13 averaged mean 2.00 p99 20.00 — as clean; "
                 "bias: frame 0.50, 13 averaged 0.40; fireflies 0.25 in a thousand\n")
        first, second = read_sides(place.format("1.00") + printed.group(1) + "antilag=false\n" + place.format("0.80"))
        self.assertEqual(first["some-place"].mean, 1.00)
        self.assertEqual(second["some-place"].mean, 0.80)

    def test_the_line_read_is_the_one_the_harness_prints(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "compare.cpp")
        printed = re.search(r'"(  \{:<28\} noise: frame mean .*?)"\s*"(.*?)"', text, re.DOTALL)
        self.assertIsNotNone(printed, "judgeNoise no longer prints a place's line where this looks for it")
        line = (printed.group(1) + printed.group(2)).removesuffix("\\n").replace("{:<28}", f"{'some-place':<28}")
        # Past the place: the frames averaged, the verdict, the frames again; every figure with
        # decimals, the p99s and the fireflies among them, is 1.25.
        line = line.replace("{:.2f}", "1.25").replace("{}", "7", 1).replace("{}", "as clean", 1).replace("{}", "7")
        self.assertEqual(read_report(line), {"some-place": Figures(1.25, 1.25, 1.25, 1.25)})


class TableTest(unittest.TestCase):
    def test_every_column_is_as_wide_as_the_longer_label(self):
        sides = (Side("own", "--bounce-reuse=own"), Side("spatiotemporal", "--bounce-reuse=spatiotemporal"))
        lines = table("still", sides, {"guild": Figures(0.69, 4, 1.8, 0.4)},
                      {"guild": Figures(0.59, 4, 1.85, 0.1)}).splitlines()
        # Each column's right edge, as the labels row sets it, is where every row's figure ends.
        heads = [match.end() for match in re.finditer(r"\S+", lines[2])]
        figures = [match.end() for match in re.finditer(r"\S+", lines[3])][1:]
        self.assertEqual(heads, figures)
        self.assertEqual(lines[1].rstrip()[-17:], "fireflies in 1000")
        self.assertEqual(len(lines[1].rstrip()), len(lines[2]))


class _Harness:
    """A build whose harness prints one place's line for each side, the second only where `versus`,
    and ends with `status`; it keeps every line it was run with."""

    def __init__(self, status: int, versus: bool = True):
        self.status = status
        self.versus = versus
        self.lines: list[tuple[str, ...]] = []

    def harness(self, verb: str, *args: str, stdout: IO[str], **options) -> subprocess.CompletedProcess:
        self.lines.append((verb, *args))
        stdout.write("  some-place                   noise: frame mean 1.00 p99 9, 13 averaged mean 2.00 p99 20 — "
                     "noisier; bias: frame 0.50, 13 averaged 0.40; fireflies 0.25 in a thousand\n")
        if self.versus:
            stdout.write("versus --antifirefly=false\n"
                         "  some-place                   noise: frame mean 0.80 p99 8, 13 averaged mean 2.00 p99 20 — "
                         "as clean; bias: frame 0.60, 13 averaged 0.40; fireflies 0.10 in a thousand\n")
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
                    self.assertIn("  some-place                       1.00     0.80  9.00  8.00   0.50   0.60      0.25"
                                  "      0.10", printed.getvalue())
                else:
                    self.assertIn(f"the run failed with status {status}", refused.getvalue())

    def test_each_leg_is_one_run_of_both_sides(self):
        built = _Harness(0)
        with tempfile.TemporaryDirectory() as out, redirect_stdout(StringIO()):
            self.assertEqual(ab(cast(Build, built), ["--ab=antifirefly", "--views=x", f"--out={out}"]), 0)
            self.assertEqual(built.lines, [
                ("noise", "--antifirefly=true", "--versus=antifirefly=false", "--strafe=150", "--views=x",
                 f"--out={Path(out) / 'strafe150'}"),
                ("noise", "--antifirefly=true", "--versus=antifirefly=false", "--walk=150", "--views=x",
                 f"--out={Path(out) / 'walk150'}"),
            ])

    def test_a_run_that_judged_one_side_is_not_read(self):
        with tempfile.TemporaryDirectory() as out, redirect_stdout(StringIO()), redirect_stderr(StringIO()) as refused:
            self.assertEqual(ab(cast(Build, _Harness(0, versus=False)), ["--ab=antifirefly", f"--out={out}"]), 1)
        self.assertIn("the run failed with status 0", refused.getvalue())


if __name__ == "__main__":
    unittest.main()
