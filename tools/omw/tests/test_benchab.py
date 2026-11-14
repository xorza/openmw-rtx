import json
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from typing import cast

from omw.benchab import Plan, Row, ab, gather, plan, read_rows, table
from omw.build import Build
from omw.noise import Side
from omw.system import Refusal


def _record(view: str, trace: tuple[float, float], frame: tuple[float, float]) -> dict:
    return {"places": [{"view": view, "frameMs": {"median": frame[0], "p99": frame[1]},
                        "gpuMs": {"trace": {"times": {"median": trace[0], "p99": trace[1]}}}}]}


class PlanTest(unittest.TestCase):
    def test_a_switch_is_its_two_sides_over_five_legs(self):
        self.assertEqual(plan(["--ab=variants", "--views=x"]),
                         Plan((Side("on", "--variants=true"), Side("off", "--variants=false")), 5, None,
                              ("--views=x",)))
        self.assertEqual(plan(["--ab=upscale=quality,off", "--legs=3", "--out=/tmp/ab"]).legs, 3)

    def test_a_relative_out_is_made_absolute_for_the_harness_that_runs_from_the_build(self):
        self.assertEqual(plan(["--ab=variants", "--out=ab"]).out, Path.cwd() / "ab")

    def test_a_line_that_gives_no_spread_or_names_its_own_record_is_refused(self):
        with self.assertRaises(Refusal):
            plan(["--ab=variants", "--legs=1"])
        with self.assertRaises(Refusal):
            plan(["--ab=variants", "--json=x.json"])


class FiguresTest(unittest.TestCase):
    def test_a_legs_record_is_read_by_place_and_row(self):
        self.assertEqual(read_rows(_record("v", (1.0, 2.0), (6.0, 9.0))),
                         {"v": {"cpu frame": (6.0, 9.0), "trace": (1.0, 2.0)}})

    def test_a_side_is_the_median_of_its_legs_and_the_spread_of_their_medians(self):
        legs = [read_rows(_record("v", (median, median + 1.0), (6.0, 9.0))) for median in (1.0, 1.4, 1.1)]
        # The medians 1.0, 1.4 and 1.1: their median 1.1, their spread 0.4; the p99s' median 2.1.
        row = gather(legs)["v"]["trace"]
        self.assertAlmostEqual(row.median, 1.1)
        self.assertAlmostEqual(row.spread, 0.4)
        self.assertAlmostEqual(row.p99, 2.1)

    def test_a_row_one_leg_lacks_is_left_out(self):
        whole = read_rows(_record("v", (1.0, 2.0), (6.0, 9.0)))
        lacking = {"v": {"cpu frame": (6.0, 9.0)}}
        self.assertEqual(set(gather([whole, lacking])["v"]), {"cpu frame"})

    def test_a_change_inside_either_spread_says_so(self):
        sides = (Side("on", "--variants=true"), Side("off", "--variants=false"))
        first = {"v": {"trace": Row(1.0, 2.0, 0.05), "bloom": Row(0.5, 0.6, 0.01)}}
        second = {"v": {"trace": Row(1.2, 2.2, 0.04), "bloom": Row(0.51, 0.6, 0.02)}}
        lines = table(sides, first, second).splitlines()
        self.assertEqual(lines[0], "--variants=true against --variants=false, ms")
        self.assertTrue(lines[3].startswith("    trace ") and lines[3].endswith("2.200"), lines[3])
        self.assertIn("+0.200", lines[3])
        self.assertTrue(lines[4].startswith("    bloom ") and lines[4].endswith("within the spread"), lines[4])


class _Harness:
    """A build whose harness writes a record wherever a leg asks for one, its trace a tenth slower on the second
    side; it keeps every line it was run with."""

    def __init__(self):
        self.lines: list[tuple[str, ...]] = []

    def build_harness(self) -> None:
        pass

    def harness(self, verb: str, *args: str, **options) -> subprocess.CompletedProcess:
        self.lines.append((verb, *args))
        record = next((arg.removeprefix("--json=") for arg in args if arg.startswith("--json=")), None)
        if record:
            trace = 1.1 if "--variants=false" in args else 1.0
            Path(record).write_text(json.dumps(_record("v", (trace, trace + 1.0), (6.0, 9.0))))
        return subprocess.CompletedProcess([verb, *args], 0)


class AbTest(unittest.TestCase):
    def test_the_sides_take_turns_after_a_warmup(self):
        built = _Harness()
        with tempfile.TemporaryDirectory() as out, redirect_stdout(StringIO()) as printed:
            self.assertEqual(ab(cast(Build, built), ["--ab=variants", "--legs=2", "--views=v", f"--out={out}"]), 0)
        sides = [next(arg for arg in line if arg.startswith("--variants=")) for line in built.lines]
        self.assertEqual(sides, ["--variants=true", "--variants=true", "--variants=false", "--variants=true",
                                 "--variants=false"])
        self.assertNotIn("--json", " ".join(built.lines[0]), "the warm-up keeps no record")
        self.assertIn("+0.100", printed.getvalue())
