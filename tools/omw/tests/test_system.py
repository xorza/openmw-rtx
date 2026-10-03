import unittest
from pathlib import Path

from omw.game import parse_folders
from omw.system import Refusal, refuse_unsupported


class SystemTest(unittest.TestCase):
    def test_linux_and_windows_are_taken_and_any_other_system_is_refused_before_any_verb(self):
        refuse_unsupported("linux")
        refuse_unsupported("win32")
        for platform in ("darwin", "freebsd14"):
            with self.assertRaises(Refusal) as refused:
                refuse_unsupported(platform)
            self.assertIn(platform, str(refused.exception))


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
