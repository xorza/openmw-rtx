import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import deps
from omw.deps import pinned_folder
from omw.pins import Pin
from omw.system import DEPS


class PinnedFolderTest(unittest.TestCase):
    def test_a_folder_is_named_after_its_pins_so_a_changed_pin_is_a_folder_not_there_yet(self):
        old = Pin("https://example.com/a", "a" * 64)
        new = Pin("https://example.com/a", "b" * 64)
        other = Pin("https://example.com/b", "c" * 64)
        self.assertEqual(pinned_folder("crash", old), pinned_folder("crash", old))
        self.assertEqual(pinned_folder("crash", old).parent, DEPS)
        self.assertTrue(pinned_folder("crash", old).name.startswith("crash-"))
        self.assertNotEqual(pinned_folder("crash", old), pinned_folder("crash", new))
        self.assertNotEqual(pinned_folder("tools", old, other), pinned_folder("tools", new, other),
                            "one pin of several changed and the folder did not")


class QuotedTest(unittest.TestCase):
    def test_a_path_is_one_powershell_string_whatever_it_holds(self):
        # Single quotes expand neither `$` nor a backtick, and a quote inside is doubled.
        self.assertEqual(deps._quoted(Path("C:/Users/o'neil/deps")), "'" + str(Path("C:/Users/o''neil/deps")) + "'")
        self.assertEqual(deps._quoted("copy_only=1"), "'copy_only=1'")
        self.assertEqual(deps._quoted("$env:PATH `n"), "'$env:PATH `n'")
        self.assertEqual(deps._quoted(""), "''")


class PruneTest(unittest.TestCase):
    def test_what_no_pin_names_goes_and_what_one_names_stays(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        with mock.patch.object(deps, "DEPS", folder):
            kept = sorted(deps.pinned_names())
            for name in kept:
                (folder / name).mkdir()
            (folder / "vulkan-sdk-0.0.1-00000000").mkdir()
            (folder / "appimage").mkdir()
            (folder / f"{kept[0]}.partial").mkdir()
            (folder / "LLVM-14.0.6-win64.exe").write_bytes(b"left over")
            deps.prune()
            self.assertEqual(sorted(path.name for path in folder.iterdir()), kept)
            deps.prune()
            self.assertEqual(sorted(path.name for path in folder.iterdir()), kept, "a second prune took more")


if __name__ == "__main__":
    unittest.main()
