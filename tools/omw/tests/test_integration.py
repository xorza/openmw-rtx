import sys
import unittest
from pathlib import Path

from omw.integration import EXAMPLE_SUITE_REVISION, script_line
from omw.system import ROOT, read_text


class SuiteRevisionTest(unittest.TestCase):
    def test_the_tree_pins_one_full_revision_on_the_line_upstream_yml_rewrites(self):
        self.assertRegex(EXAMPLE_SUITE_REVISION, r"^[0-9a-f]{40}$")
        lines = read_text(ROOT / "tools" / "omw" / "integration.py").splitlines()
        self.assertEqual(lines.count(f'EXAMPLE_SUITE_REVISION = "{EXAMPLE_SUITE_REVISION}"'), 1)


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
