import contextlib
import os
import tempfile
import unittest
from pathlib import Path

from omw.game import parse_folders
from omw.system import EXE, Refusal, on_path, refuse_unsupported, resolved


class SystemTest(unittest.TestCase):
    def test_linux_and_windows_are_taken_and_any_other_system_is_refused_before_any_verb(self):
        refuse_unsupported("linux")
        refuse_unsupported("win32")
        for platform in ("darwin", "freebsd14"):
            with self.assertRaises(Refusal) as refused:
                refuse_unsupported(platform)
            self.assertIn(platform, str(refused.exception))

    def test_a_relative_program_is_read_from_the_directory_it_runs_in(self):
        build = Path("build-release")
        absolute = str(Path.cwd() / "openmw")
        for program, expected in (("./openmw-rtxtool", str(build.absolute() / "openmw-rtxtool")),
                                  ("bin/openmw", str(build.absolute() / "bin" / "openmw")),
                                  (absolute, absolute),
                                  ("cmake", "cmake")):
            self.assertEqual(resolved([program, "info"], None, build), [expected, "info"], program)
        self.assertEqual(resolved(["./openmw-rtxtool"], None), ["./openmw-rtxtool"])

    def test_a_bare_name_is_found_on_the_path_alone_and_not_in_the_working_directory(self):
        with tempfile.TemporaryDirectory() as root:
            here, listed, empty = Path(root, "here"), Path(root, "listed"), Path(root, "empty")
            for folder in (here, listed, empty):
                folder.mkdir()
            for folder in (here, listed):
                tool = folder / f"tool{EXE}"
                tool.write_bytes(b"")
                tool.chmod(0o755)
            with contextlib.chdir(here):
                self.assertEqual(os.path.normcase(on_path("tool", os.pathsep.join([str(empty), str(listed)]))),
                                 os.path.normcase(listed / f"tool{EXE}"))
                self.assertIsNone(on_path("tool", str(empty)))
                self.assertIsNone(on_path("tool", ""))


class FoldersTest(unittest.TestCase):
    def test_the_two_folders_are_read_among_the_runs_other_lines(self):
        printed = ("[04:51:08.253 *] Crash reports go to /home/someone/.local/share/openmw/crashes\n"
                   "config /home/someone/.config/openmw\n"
                   "data /home/someone/.local/share/open mw\n")
        folders = parse_folders(printed)
        self.assertEqual(folders.config, Path("/home/someone/.config/openmw"))
        self.assertEqual(folders.data, Path("/home/someone/.local/share/open mw"))

    def test_a_run_that_named_no_folders_is_refused(self):
        with self.assertRaises(Refusal):
            parse_folders("config /somewhere\n")


if __name__ == "__main__":
    unittest.main()
