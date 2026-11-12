import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import formatting

# A stand-in for clang-format that lays out one rule: no space inside parentheses. It reads stdin
# and writes stdout, as clang-format does under `--assume-filename`, and refuses a file that asks it to.
FORMATTER = """import re, sys
text = sys.stdin.read()
if "refuse" in text:
    sys.exit(1)
sys.stdout.write(re.sub(r"\\s+\\)", ")", re.sub(r"\\(\\s+", "(", text)))
"""


class MergeFileTest(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.folder)
        script = self.folder / "formatter.py"
        script.write_text(FORMATTER)
        self.formatter = self.folder / "clang-format"
        self.formatter.write_text(f"#!/bin/sh\nexec '{sys.executable}' '{script}' \"$@\"\n")
        self.formatter.chmod(0o755)

    def sides(self, base: str, ours: str, theirs: str) -> list[Path]:
        paths = [self.folder / name for name in ("base", "ours", "theirs")]
        for path, text in zip(paths, (base, ours, theirs), strict=True):
            path.write_text(text)
        return paths

    @unittest.skipIf(sys.platform == "win32", "the stand-in formatter is a shell script")
    def test_a_side_reformatted_merges_with_an_edit_made_before_the_reformat(self):
        # The fork reformatted every line and changed the first; upstream changed the third in the old
        # layout. Line for line, every line differs on both sides, so git alone stops at a conflict.
        base = "int a(  1 );\nint b(  2 );\nint c(  3 );\n"
        ours = "int a(10);\nint b(2);\nint c(3);\n"
        theirs = "int a(  1 );\nint b(  2 );\nint c(  30 );\n"

        plain_base, plain_ours, plain_theirs = self.sides(base, ours, theirs)
        plain = subprocess.run(["git", "merge-file", "-q", plain_ours, plain_base, plain_theirs], check=False)
        self.assertGreater(plain.returncode, 0, "git alone merged the reformat, so the test proves nothing")

        merged_base, merged_ours, merged_theirs = self.sides(base, ours, theirs)
        with mock.patch.object(formatting.deps, "clang_format", return_value=self.formatter):
            code = formatting.merge_file([str(merged_base), str(merged_ours), str(merged_theirs), "7", "x.cpp"])
        self.assertEqual(code, 0)
        self.assertEqual(merged_ours.read_text(), "int a(10);\nint b(2);\nint c(30);\n")

    @unittest.skipIf(sys.platform == "win32", "the stand-in formatter is a shell script")
    def test_a_real_conflict_stays_one_and_a_text_the_formatter_refuses_merges_as_it_is(self):
        base_path, ours_path, theirs_path = self.sides("int a(  1 );\n", "int a(10);\n", "int a(  20 );\n")
        with mock.patch.object(formatting.deps, "clang_format", return_value=self.formatter):
            code = formatting.merge_file([str(base_path), str(ours_path), str(theirs_path), "7", "x.cpp"])
        self.assertEqual(code, 1, "one conflict, the line both sides changed")
        self.assertEqual(ours_path.read_text(),
                         "<<<<<<< x.cpp (ours)\nint a(10);\n=======\nint a(20);\n>>>>>>> x.cpp (theirs)\n")

        self.assertEqual(formatting.formatted_text(self.formatter, b"refuse(  1 )\n", "x.cpp"), b"refuse(  1 )\n")
        self.assertEqual(formatting.formatted_text(self.formatter, b"keep(  1 )\n", "x.cpp"), b"keep(1)\n")

    def test_the_driver_is_handed_five_arguments(self):
        with self.assertRaises(formatting.Refusal):
            formatting.merge_file(["base", "ours", "theirs", "7"])


class FormatTreeTest(unittest.TestCase):
    def test_named_files_narrow_the_tree_to_its_own_sources_and_check_changes_nothing(self):
        ran = []
        codes = {"cf": 0, "g": 0}

        def run(command, **_options):
            ran.append(command)
            return subprocess.CompletedProcess(command, codes[command[0]])

        def files(*pathspecs):
            if pathspecs == formatting.CMAKE_SOURCES:
                return ["CMakeLists.txt", "cmake/x.cmake"]
            return ["a.cpp", "b.hpp", "c.h"]

        with (mock.patch.object(formatting, "working_tree_files", side_effect=files),
              mock.patch.object(formatting.deps, "clang_format", return_value=Path("cf")),
              mock.patch.object(formatting.deps, "gersemi", return_value=["g"]),
              mock.patch.object(formatting.subprocess, "run", side_effect=run)):
            named = ["./a.cpp", "c.h", "extern/x.cpp", "README.md", "CMakeLists.txt"]
            self.assertEqual(formatting.format_tree(named), 0)
            self.assertEqual(ran, [["cf", "-i", "a.cpp", "c.h"],
                                   ["g", "--in-place", "--no-cache", "--quiet", "CMakeLists.txt"]])
            ran.clear()
            # No CMake file named, no gersemi.
            self.assertEqual(formatting.format_tree(["b.hpp"]), 0)
            self.assertEqual(ran, [["cf", "-i", "b.hpp"]])
            ran.clear()
            self.assertEqual(formatting.format_tree(["--check"]), 0)
            self.assertEqual(ran, [["cf", "--dry-run", "-Werror", "a.cpp", "b.hpp", "c.h"],
                                   ["g", "--check", "--diff", "--no-cache", "--quiet", "CMakeLists.txt",
                                    "cmake/x.cmake"]])
            # A CMake file out of its layout fails the check as a C++ source does.
            codes["g"] = 1
            self.assertEqual(formatting.format_tree(["--check"]), 1)
            with self.assertRaises(formatting.Refusal):
                formatting.format_tree(["--fix"])


class SourcesTest(unittest.TestCase):
    def test_the_tree_formats_its_own_cmake_and_no_copy_of_another_projects(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root)
        names = ["CMakeLists.txt", "cmake/Flags.cmake", "apps/x/CMakeLists.txt", "extern/CMakeLists.txt",
                 "extern/oics/CMakeLists.txt", "vcpkg/triplets/x64-linux-openmw.cmake",
                 "vcpkg/ports/uriparser/portfile.cmake", "README.md"]
        for name in names:
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text("x\n")
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        self.assertEqual(sorted(formatting.working_tree_files(*formatting.CMAKE_SOURCES, root=root)),
                         ["CMakeLists.txt", "apps/x/CMakeLists.txt", "cmake/Flags.cmake", "extern/CMakeLists.txt",
                          "vcpkg/triplets/x64-linux-openmw.cmake"])


class ChangedFilesTest(unittest.TestCase):
    def test_what_changed_against_head_and_what_is_new_and_nothing_deleted(self):
        # A `TemporaryDirectory`, whose cleanup makes a read-only file writable before removing it, as Windows
        # removes none of git's objects otherwise.
        root = Path(self.enterContext(tempfile.TemporaryDirectory()))
        git = ["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@t"]
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        for name in ("same.cpp", "edited.cpp", "staged.cpp", "gone.cpp"):
            (root / name).write_text("int a;\n")
        (root / ".gitignore").write_text("ignored.cpp\n")
        subprocess.run([*git, "add", "."], check=True)
        subprocess.run([*git, "commit", "-q", "-m", "x"], check=True)
        self.assertEqual(formatting.changed_files(root), [], "a clean tree names a file")

        (root / "edited.cpp").write_text("int b;\n")
        (root / "staged.cpp").write_text("int c;\n")
        subprocess.run([*git, "add", "staged.cpp"], check=True)
        (root / "gone.cpp").unlink()
        (root / "new.cpp").write_text("int d;\n")
        (root / "ignored.cpp").write_text("int e;\n")
        self.assertEqual(formatting.changed_files(root), ["edited.cpp", "new.cpp", "staged.cpp"])


if __name__ == "__main__":
    unittest.main()
