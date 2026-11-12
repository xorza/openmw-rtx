import json
import shutil
import tempfile
import unittest
from pathlib import Path

from omw.build import FLAVOURS
from omw.package import CONTAINER_DIR, DIST
from omw.presets import CACHE_SEEDS, digest, test_environment
from omw.system import DEPS, OUT, ROOT, WINDOWS, Refusal, build_dir, environment_key, read_text


def presets_root(test: unittest.TestCase, content: dict) -> Path:
    root = Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, root)
    (root / "CMakePresets.json").write_text(json.dumps(content))
    for seed in CACHE_SEEDS:
        (root / seed).parent.mkdir(parents=True, exist_ok=True)
        (root / seed).write_text("set(flags -O2)\n")
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
        # Each file that seeds the cache's flags, as an edit to one reaches a build directory only when configured
        # afresh; and back to its text, the digest it had.
        for seed in CACHE_SEEDS:
            (root / seed).write_text("set(flags -O3)\n")
            self.assertNotEqual(digest({"PRESET_READS": "1"}, root), base, seed)
            (root / seed).write_text("set(flags -O2)\n")
            self.assertEqual(digest({"PRESET_READS": "1"}, root), base, seed)

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
             "environment": {"OPTIONS": "fixed:$penv{OPTIONS}", "HOME_TOO": "$env{HOME}/x", "Cased": "$env{Home}"}},
        ]})
        env = {"OPTIONS": "mine=1", "HOME": "/h", "GONE": "here", "KEPT": "k"}
        # A name in another case is the same variable where the system reads names without case.
        self.assertEqual(test_environment("leaf", env, root), {
            "OPTIONS": "fixed:mine=1", "HOME_TOO": "/h/x", "HOME": "/h", "KEPT": "k",
            "A": "base", "B": "base", "C": "other", environment_key("Cased"): "/h" if WINDOWS else "",
        })

    def test_a_preset_that_is_not_there_is_refused(self):
        with self.assertRaises(Refusal):
            test_environment("missing", {}, presets_root(self, {"version": 10}))



class BuildDirTest(unittest.TestCase):
    def test_each_flavour_builds_where_its_preset_says_and_apart_from_what_else_the_driver_writes(self):
        configure = json.loads(read_text(ROOT / "CMakePresets.json"))["configurePresets"]
        presets = {preset["name"]: preset for preset in configure}
        for flavour in FLAVOURS:
            with self.subTest(flavour=flavour):
                written = presets[flavour]["binaryDir"].replace("${sourceDir}", ROOT.as_posix())
                self.assertEqual(Path(written), build_dir(flavour))
                self.assertEqual(build_dir(flavour).parent, OUT)
                self.assertNotIn(build_dir(flavour), (DEPS, DIST, CONTAINER_DIR))


if __name__ == "__main__":
    unittest.main()
