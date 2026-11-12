import contextlib
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw.system import (
    EXE,
    UPSTREAM,
    Refusal,
    environment_key,
    fetch_upstream,
    files_upstream_lacks,
    on_path,
    parse_set_output,
    refuse_unsupported,
    report,
    resolved,
)


class SystemTest(unittest.TestCase):
    def test_a_check_s_findings_are_said_and_any_one_fails_it(self):
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            self.assertEqual(report([]), 0)
            self.assertEqual(report(["a.cpp:1: one", "b.cpp:2: two"]), 1)
        self.assertEqual(said.getvalue(), "a.cpp:1: one\nb.cpp:2: two\n")

    def test_the_forks_files_are_the_working_trees_that_upstreams_tree_lacks_at_the_merge_base(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)

            # Away from the user's and the system's configuration, whose signing or hooks would fail a commit.
            alone = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}

            def git(*args: str) -> None:
                subprocess.run(["git", "-C", root, "-c", "user.name=t", "-c", "user.email=t@t", *args],
                               check=True, capture_output=True, env=alone)

            git("init", "-q", "-b", "master")
            (root / "upstream.cpp").write_text("")
            (root / "extern").mkdir()
            (root / "extern" / "vendored.cpp").write_text("")
            git("add", ".")
            git("commit", "-q", "-m", "upstream")
            git("update-ref", UPSTREAM, "HEAD")
            (root / "committed.cpp").write_text("")
            git("add", "committed.cpp")
            git("commit", "-q", "-m", "fork")
            (root / "new.cpp").write_text("")
            (root / "extern" / "added.cpp").write_text("")
            (root / "upstream.cpp").write_text("changed")

            self.assertEqual(files_upstream_lacks(root), {"committed.cpp", "new.cpp"})

    def test_upstreams_master_is_fetched_into_the_drivers_own_ref_and_a_failed_fetch_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            alone = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
            source, root = Path(folder) / "upstream", Path(folder) / "fork"

            def git(*args: str) -> str:
                return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args],
                                      check=True, capture_output=True, text=True, env=alone).stdout.strip()

            git("init", "-q", "-b", "master", str(source))
            git("-C", str(source), "commit", "-q", "--allow-empty", "-m", "upstream")
            git("init", "-q", str(root))
            with mock.patch.dict(os.environ, alone):
                fetch_upstream(root, str(source))
                self.assertEqual(git("-C", str(root), "rev-parse", UPSTREAM), git("-C", str(source), "rev-parse", "HEAD"))
                self.assertEqual(git("-C", str(root), "remote"), "", "no remote is configured")
                with self.assertRaisesRegex(Refusal, "upstream's master, which tells the fork's files from upstream's, "
                                                     "could not be fetched from .*absent"):
                    fetch_upstream(root, str(Path(folder) / "absent"))

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

            # Each of `PATHEXT`'s extensions is tried on Windows, where Python 3.11's `which` tries
            # none for a name with a folder, and a name that carries one is taken as it is.
            sdk = listed / "glslc.EXE"
            sdk.write_bytes(b"")
            sdk.chmod(0o755)
            with mock.patch("omw.system.WINDOWS", True), mock.patch.dict(os.environ, {"PATHEXT": ".COM;.EXE"}):
                for program, expected in (("glslc", sdk), ("glslc.EXE", sdk), ("glslc.COM", None)):
                    found = on_path(program, str(listed))
                    self.assertEqual(found and os.path.normcase(found), expected and os.path.normcase(expected),
                                     program)


class ParseSetOutputTest(unittest.TestCase):
    def test_the_variables_and_not_the_drives(self):
        text = "A=1\r\n=C:=C:\\work\r\nPath=x;y\r\nEMPTY=\r\nno equals sign\r\nB=c=d\r\n"
        self.assertEqual(parse_set_output(text), {
            environment_key("A"): "1", environment_key("Path"): "x;y", environment_key("EMPTY"): "",
            environment_key("B"): "c=d",
        })

if __name__ == "__main__":
    unittest.main()
