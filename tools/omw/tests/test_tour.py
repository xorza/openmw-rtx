import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from typing import cast

from omw.build import Build
from omw.exitstatus import ExitStatus
from omw.system import Refusal
from omw.tour import FINDINGS, Content, Outcome, plan, read_counts, run, table, wants_each, worst


class PlanTest(unittest.TestCase):
    def test_each_content_is_a_label_and_its_line_and_the_rest_goes_to_every_tour(self):
        asked = plan(["--each=vanilla:--content=Tribunal.esm --content=Bloodmoon.esm",
                      "--each=mfr:--replace=config --config='/home/a b/openmw-mfr'", "--speed=6000", "--out=tours"])
        self.assertEqual(asked.contents, (
            Content("vanilla", ("--content=Tribunal.esm", "--content=Bloodmoon.esm")),
            Content("mfr", ("--replace=config", "--config=/home/a b/openmw-mfr"))))
        self.assertEqual(asked.rest, ("--speed=6000",))
        self.assertEqual(asked.out, Path.cwd() / "tours", "absolute, for the harness that runs from the build")
        self.assertTrue(wants_each(["--each=a:"]))
        self.assertFalse(wants_each(["--plan"]))

    def test_a_line_that_names_no_label_or_one_twice_is_refused(self):
        for line in (["--each=--content=x"], ["--each=a b:x"], ["--each=a:x", "--each=a:y"]):
            with self.subTest(line=line), self.assertRaises(Refusal):
                plan(line)


class ReportTest(unittest.TestCase):
    def test_the_counts_are_read_off_the_findings(self):
        self.assertEqual(read_counts("\ntour findings: 2 faults, 7 costs and marks\n..."), (2, 7))
        self.assertIsNone(read_counts("nothing here"))

    def test_the_worst_status_is_a_refusal_then_a_failure_then_a_difference(self):
        self.assertEqual(worst([0, 3, 1]), ExitStatus.FAILED)
        self.assertEqual(worst([0, 2, 1]), ExitStatus.REFUSED)
        self.assertEqual(worst([0, 3]), ExitStatus.DIFFERED)
        self.assertEqual(worst([0, 0]), ExitStatus.PASSED)
        self.assertEqual(worst([0, -11]), ExitStatus.FAILED, "a crash's signal is a failure")
        self.assertEqual(worst([]), ExitStatus.PASSED)

    def test_the_table_is_a_content_a_line(self):
        self.assertEqual(table([Outcome("vanilla", 0, 0, 3), Outcome("mfr", -11, None, None)]).splitlines(), [
            "  content          status      faults   costs",
            "  vanilla          passed           0       3",
            "  mfr              -11              -       -"])


class _Harness:
    """A build whose tour writes its findings into the folder its line names, failed for the content named `bad`."""

    def __init__(self):
        self.lines: list[tuple[str, ...]] = []

    def build_harness(self) -> None:
        pass

    def harness(self, verb: str, *args: str, **options) -> subprocess.CompletedProcess:
        self.lines.append((verb, *args))
        folder = Path(next(arg.removeprefix("--out=") for arg in args if arg.startswith("--out=")))
        folder.mkdir(parents=True)
        bad = "bad" in folder.name
        (folder / FINDINGS).write_text(f"\ntour findings: {1 if bad else 0} faults, 4 costs and marks\n")
        return subprocess.CompletedProcess([verb, *args], 1 if bad else 0)


class RunTest(unittest.TestCase):
    def test_the_contents_are_toured_in_turn_and_the_worst_status_is_the_runs(self):
        built = _Harness()
        with tempfile.TemporaryDirectory() as out, redirect_stdout(StringIO()) as printed, \
                redirect_stderr(StringIO()):
            status = run(cast(Build, built), ["--each=good:--content=a.esm", "--each=bad:--config=b", "--plan",
                                              f"--out={out}"])
            self.assertEqual(built.lines, [
                ("tour", "--content=a.esm", "--plan", f"--out={Path(out) / 'good'}"),
                ("tour", "--config=b", "--plan", f"--out={Path(out) / 'bad'}")])
            self.assertTrue((Path(out) / "good.log").is_file())
        self.assertEqual(status, ExitStatus.FAILED)
        self.assertIn("  bad              failed           1       4", printed.getvalue())


if __name__ == "__main__":
    unittest.main()
