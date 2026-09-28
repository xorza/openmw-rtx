import json
import shutil
import tempfile
import unittest
from pathlib import Path

from omw.presets import digest, test_environment
from omw.system import Refusal


def presets_root(test: unittest.TestCase, content: dict) -> Path:
    root = Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, root)
    (root / "CMakePresets.json").write_text(json.dumps(content))
    return root


class DigestTest(unittest.TestCase):
    def test_what_a_configure_reads_moves_the_digest_and_nothing_else_does(self):
        root = presets_root(self, {"version": 10, "configurePresets": [
            {"name": "a", "cacheVariables": {"X": "$env{PRESET_READS}"}}]})
        base = digest({"PRESET_READS": "1"}, root)
        self.assertEqual(digest({"PRESET_READS": "1", "UNREAD": "x"}, root), base)
        self.assertNotEqual(digest({"PRESET_READS": "2"}, root), base)
        self.assertNotEqual(digest({"PRESET_READS": "1", "CXX": "clang++"}, root), base)
        self.assertNotEqual(digest({"PRESET_READS": "1", "VULKAN_SDK": "/sdk"}, root), base)

    def test_a_presets_file_that_includes_another_is_refused(self):
        root = presets_root(self, {"version": 10, "include": ["other.json"]})
        with self.assertRaises(Refusal):
            digest({}, root)


class TestEnvironmentTest(unittest.TestCase):
    def test_a_preset_inherits_expands_and_takes_away(self):
        root = presets_root(self, {"version": 10, "testPresets": [
            {"name": "base", "hidden": True, "environment": {"A": "base", "B": "base", "GONE": None}},
            {"name": "other", "hidden": True, "environment": {"A": "other", "C": "other"}},
            {"name": "leaf", "inherits": ["base", "other"],
             "environment": {"OPTIONS": "fixed:$penv{OPTIONS}", "HOME_TOO": "$env{HOME}/x"}},
        ]})
        env = {"OPTIONS": "mine=1", "HOME": "/h", "GONE": "here", "KEPT": "k"}
        self.assertEqual(test_environment("leaf", env, root), {
            "OPTIONS": "fixed:mine=1", "HOME_TOO": "/h/x", "HOME": "/h", "KEPT": "k",
            "A": "base", "B": "base", "C": "other",
        })

    def test_a_preset_that_is_not_there_is_refused(self):
        with self.assertRaises(Refusal):
            test_environment("missing", {}, presets_root(self, {"version": 10}))


if __name__ == "__main__":
    unittest.main()
