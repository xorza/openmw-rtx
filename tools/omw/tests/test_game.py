import contextlib
import io
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from omw.game import parse_folders, setup
from omw.system import Refusal


class ImportingBuild:
    """A build whose importer appends a line to the file it is handed, or fails having written half
    of one, and whose harness names `config` as the game's configuration folder."""

    def __init__(self, config: Path, fails: bool):
        self.config = config
        self.fails = fails

    def harness(self, verb: str, *args, **options) -> subprocess.CompletedProcess:
        return subprocess.CompletedProcess(args, 0, stdout=f"config {self.config}\ndata {self.config}\n")

    def build(self, targets: list[str]) -> None:
        pass

    def binary(self, name: str) -> str:
        return name

    def run_here(self, command: list, check: bool = False, **options) -> subprocess.CompletedProcess:
        with open(command[-1], "a") as cfg:
            cfg.write("fallback=Weather_" if self.fails else "fallback=Weather_Sunrise_Time,6\n")
        if self.fails:
            raise subprocess.CalledProcessError(1, command)
        return subprocess.CompletedProcess(command, 0)


class SetupTest(unittest.TestCase):
    def test_the_configuration_is_named_only_once_the_import_is_whole(self):
        root = Path(tempfile.mkdtemp(prefix="omw-setup-test-"))
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        install = root / "Morrowind"
        (install / "Data Files").mkdir(parents=True)
        (install / "Morrowind.ini").write_text("[General]\n")
        config = root / "config"
        cfg = config / "openmw.cfg"

        with self.assertRaises(subprocess.CalledProcessError):
            setup(ImportingBuild(config, fails=True), [str(install)])
        self.assertEqual(sorted(config.iterdir()), [], "a failed import left a file behind")

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(setup(ImportingBuild(config, fails=False), [str(install)]), 0)
        self.assertEqual(cfg.read_text(),
                         f'data="{install.resolve() / "Data Files"}"\nfallback=Weather_Sunrise_Time,6\n')
        self.assertEqual(sorted(config.iterdir()), [cfg])


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
