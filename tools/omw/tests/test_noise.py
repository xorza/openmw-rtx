import json
import re
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from typing import IO, cast

from omw.build import Build
from omw.noise import RECORD, Figures, Leg, Plan, Side, ab, leg_log, plan, read_record, table, wants_ab
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

    def test_a_legs_log_keeps_the_whole_of_its_name(self):
        out = Path("/tmp/ab")
        self.assertEqual(leg_log(out / "walk1.5"), out / "walk1.5.log")
        self.assertNotEqual(leg_log(out / "walk1.5"), leg_log(out / "walk1"))
        self.assertEqual(leg_log(out / "still"), out / "still.log")

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
            (["--ab="], "--ab= names no switch: `--ab=antifirefly` or `--ab=noise=blue-noise,white-hash`"),
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


class RecordTest(unittest.TestCase):
    def test_every_side_is_read_by_place_with_the_figures_the_table_shows(self):
        record = {"sides": [
            [{"place": "guild", "mean": 1.24, "p99": 12.35, "barMean": 3.59, "barP99": 27.0, "bias": 2.1,
              "barBias": 1.41, "fireflies": 0.4, "clean": True}],
            [{"place": "guild", "mean": 0.49, "p99": 3.0, "barMean": 2.7, "barP99": 21.0, "bias": 1.57,
              "barBias": 0.53, "fireflies": 0.0, "clean": False}],
        ]}
        self.assertEqual(read_record(record), ({"guild": Figures(1.24, 12.35, 2.1, 0.4)},
                                               {"guild": Figures(0.49, 3.0, 1.57, 0.0)}))
        self.assertEqual(read_record({"sides": [[]]}), ({},))

    def test_the_record_is_where_the_harness_writes_it(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "noise.hpp")
        named = re.search(r'sNoiseRecord = "([^"]+)"', text)
        self.assertIsNotNone(named, "noise.hpp no longer names the record where this looks for it")
        self.assertEqual(named.group(1), RECORD)


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
    """A build whose harness writes one place's figures for each side into its --out, the second only
    where `versus`, and ends with `status`; it keeps every line it was run with."""

    def __init__(self, status: int, versus: bool = True):
        self.status = status
        self.versus = versus
        self.lines: list[tuple[str, ...]] = []

    def build_harness(self) -> None:
        pass

    def harness(self, verb: str, *args: str, stdout: IO[str], **options) -> subprocess.CompletedProcess:
        self.lines.append((verb, *args))
        out = Path(next(arg for arg in args if arg.startswith("--out=")).removeprefix("--out="))
        out.mkdir(parents=True, exist_ok=True)
        sides = [[{"place": "some-place", "mean": 1.0, "p99": 9.0, "barMean": 2.0, "barP99": 20.0, "bias": 0.5,
                   "barBias": 0.4, "fireflies": 0.25, "clean": False}]]
        if self.versus:
            sides.append([{"place": "some-place", "mean": 0.8, "p99": 8.0, "barMean": 2.0, "barP99": 20.0,
                           "bias": 0.6, "barBias": 0.4, "fireflies": 0.1, "clean": True}])
        (out / RECORD).write_text(json.dumps({"sides": sides}))
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
