import io
import os
import shutil
import tarfile
import tempfile
import unittest
import zipfile
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


class CMakeTest(unittest.TestCase):
    def test_the_pinned_cmake_is_unpacked_whole_once_and_put_first_on_the_path(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        suffix = ".zip" if deps.WINDOWS else ".tar.gz"
        inner = "cmake-9.9.9-test"
        pin = Pin(f"https://example.com/{inner}{suffix}", "d" * 64)

        def download(_pin: Pin, file: Path) -> None:
            program = b"#!/bin/sh\n"
            if deps.WINDOWS:
                with zipfile.ZipFile(file, "w") as zipped:
                    zipped.writestr(f"{inner}/bin/cmake.exe", program)
            else:
                with tarfile.open(file, "w:gz") as tarred:
                    entry = tarfile.TarInfo(f"{inner}/bin/cmake")
                    entry.size = len(program)
                    entry.mode = 0o755
                    tarred.addfile(entry, io.BytesIO(program))

        with (mock.patch.object(deps, "DEPS", folder), mock.patch.dict(deps.pins.CMAKE, {deps.SYSTEM: pin}),
              mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
            found = deps.cmake()
            self.assertEqual(found, deps.cmake_dir())
            self.assertEqual(found.parent, folder)
            self.assertTrue((found / "bin" / f"cmake{deps.EXE}").is_file())
            if not deps.WINDOWS:
                self.assertTrue(os.access(found / "bin" / "cmake", os.X_OK), "the program lost its mode")
            self.assertEqual(sorted(path.name for path in folder.iterdir()), [found.name], "the archive stayed")

            self.assertEqual(deps.cmake(), found)
            self.assertEqual(fetched.call_count, 1, "a CMake already there was fetched again")

            env = {deps.environment_key("PATH"): "/elsewhere"}
            deps.cmake_environment(env)
            self.assertEqual(env[deps.environment_key("PATH")], os.pathsep.join([str(found / "bin"), "/elsewhere"]))


class PruneTest(unittest.TestCase):
    def test_what_no_pin_names_goes_and_what_one_names_stays(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        with mock.patch.object(deps, "DEPS", folder):
            kept = sorted(deps.pinned_names())
            # Every getter's folder, the formatters' among them, which a prune once took.
            self.assertIn(deps.clang_format_dir().name, kept)
            self.assertIn(deps.gersemi_dir().name, kept)
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
