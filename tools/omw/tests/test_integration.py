import sys
import unittest
from pathlib import Path

from omw.integration import _PIPELINE, script_line, suite_revision
from omw.system import ROOT, Refusal, read_text

A = "a1ba6fc9d7315c2158ef4c60349fc4e71b7ca343"
B = "0123456789abcdef0123456789abcdef01234567"


class SuiteRevisionTest(unittest.TestCase):
    def test_the_one_revision_the_pipeline_pins_is_read_however_often_it_is_named(self):
        pipeline = f"variables:\n  EXAMPLE_SUITE_REVISION: {A}\njob:\n  variables:\n    EXAMPLE_SUITE_REVISION: {A} \n"
        self.assertEqual(suite_revision(pipeline), A)

    def test_two_revisions_or_none_are_refused(self):
        for pipeline in (
            f"  EXAMPLE_SUITE_REVISION: {A}\n  EXAMPLE_SUITE_REVISION: {B}\n",
            "variables:\n  OTHER: 1\n",
            # A branch name or a short hash names no one tree.
            "  EXAMPLE_SUITE_REVISION: master\n",
            f"  EXAMPLE_SUITE_REVISION: {A[:12]}\n",
        ):
            with self.subTest(pipeline=pipeline), self.assertRaises(Refusal):
                suite_revision(pipeline)

    def test_the_tree_pins_one_revision(self):
        self.assertRegex(suite_revision(read_text(_PIPELINE)), r"^[0-9a-f]{40}$")


class ScriptLineTest(unittest.TestCase):
    def test_each_part_of_the_suite_is_handed_over_by_its_data_folder_in_the_pipelines_order(self):
        suite = Path("/suite")
        line = script_line(Path("/build/openmw"), Path("/build/out"), suite, ["--test_filter=findPath"])
        self.assertEqual(line, [
            sys.executable, ROOT / "scripts" / "integration_tests.py",
            "--omw", Path("/build/openmw"),
            "--workdir", Path("/build/out"),
            "--config", suite,
            "--data", suite / "game_template" / "data",
            "--data", suite / "example_animated_creature" / "data",
            "--data", suite / "the_hub" / "data",
            "--data", suite / "integration_tests" / "data",
            "--data", suite / "example_static_props" / "data",
            "--test_filter=findPath",
            ROOT / "scripts" / "data" / "integration_tests",
        ])


if __name__ == "__main__":
    unittest.main()
