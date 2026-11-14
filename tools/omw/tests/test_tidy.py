import contextlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import tidy
from omw.system import ROOT
from omw.tidy import Source

BUILD = ROOT / "out" / "debug"


def native(path: str) -> str:
    """`path` as the system spells a plain path: `os.path.normpath` gives Windows its own separator."""
    return path.replace("/", os.sep)


def entry(file: Path, output: str) -> dict:
    return {"directory": str(BUILD), "file": str(file), "output": output, "command": "clang++ -c"}


class LintedSourcesTest(unittest.TestCase):
    def test_the_sources_of_apps_and_components_each_once_in_path_order_and_nothing_vendored_or_generated(self):
        app = ROOT / "apps" / "openmw" / "main.cpp"
        component = ROOT / "components" / "rtx" / "a.cpp"
        database = [
            entry(component, "components/a.o"),
            entry(ROOT / "extern" / "x.cpp", "extern/x.o"),
            entry(BUILD / "apps" / "openmw" / "generated.cpp", "apps/generated.o"),
            entry(app, "apps/main.o"),
            # One source in two targets, as a test binary compiles a component's file again.
            entry(app, "apps/tests/main.o"),
        ]
        self.assertEqual(tidy.linted_sources(database),
                         [Source(app, BUILD / "apps/main.o"), Source(component, BUILD / "components/a.o")])


class SourcesOfTest(unittest.TestCase):
    def test_a_named_source_and_the_sources_that_read_a_named_header(self):
        listing = (
            "apps/a.o: #deps 2, deps mtime 1 (VALID)\n"
            f"    {ROOT}/apps/a.cpp\n"
            f"    {ROOT}/components/shared.hpp\n"
            "\n"
            "apps/b.o: #deps 1, deps mtime 1 (VALID)\n"
            f"    {ROOT}/apps/b.cpp\n"
            "\n"
            "apps/c.o: #deps 2, deps mtime 1 (VALID)\n"
            f"    {ROOT}/apps/c.cpp\n"
            "    ../../components/own.hpp\n"
        )
        read = tidy.headers_by_object(listing, BUILD)
        self.assertEqual(read[(BUILD / "apps/c.o").resolve()],
                         {(ROOT / "apps/c.cpp").resolve(), (ROOT / "components/own.hpp").resolve()},
                         "a relative input is from the build directory")

        a = Source(ROOT / "apps/a.cpp", BUILD / "apps/a.o")
        b = Source(ROOT / "apps/b.cpp", BUILD / "apps/b.o")
        c = Source(ROOT / "apps/c.cpp", BUILD / "apps/c.o")
        never_built = Source(ROOT / "apps/d.cpp", BUILD / "apps/d.o")
        sources = [a, b, c]
        # A header reaches the sources that read it: shared.hpp is a's alone.
        self.assertEqual(tidy.sources_of([ROOT / "components/shared.hpp"], sources, read), [a])
        # A source named is itself.
        self.assertEqual(tidy.sources_of([ROOT / "apps/b.cpp"], sources, read), [b])
        self.assertEqual(tidy.sources_of([ROOT / "components/own.hpp", ROOT / "apps/b.cpp"], sources, read), [b, c])
        # A file nothing reads reaches nothing.
        self.assertEqual(tidy.sources_of([ROOT / "README.md"], sources, read), [])
        # A source the deps log has no line for is taken, as what it reads is unknown.
        self.assertEqual(tidy.sources_of([ROOT / "README.md"], [*sources, never_built], read), [never_built])


class FindingsTest(unittest.TestCase):
    def test_each_diagnostic_with_its_notes_and_its_checks_without_the_errors_marker(self):
        said = (
            "/a/b.hpp:3:7: error: use 'std::scoped_lock' [modernize-use-scoped-lock,-warnings-as-errors]\n"
            "    3 |       std::lock_guard lock(m);\n"
            "      |       ^\n"
            "/a/c.cpp:9:1: warning: narrowing [bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conv]\n"
            "/a/c.cpp:8:1: note: declared here\n"
        )
        found = tidy.findings(said)
        self.assertEqual([finding.first for finding in found], [said.splitlines()[0], said.splitlines()[3]])
        self.assertEqual(found[0].text, "\n".join(said.splitlines()[0:3]))
        self.assertEqual(found[0].checks, ("modernize-use-scoped-lock",))
        self.assertEqual(found[1].text, "\n".join(said.splitlines()[3:5]), "a note belongs to the diagnostic before it")
        self.assertEqual(found[1].checks,
                         ("bugprone-narrowing-conversions", "cppcoreguidelines-narrowing-conv"))
        # One header reached through two includers' paths is one finding.
        through = tidy.findings("/a/x/../y/z.hpp:1:2: warning: w [c]\n/a/y/z.hpp:1:2: warning: w [c]\n")
        self.assertEqual({finding.key for finding in through}, {native("/a/y/z.hpp") + ":1:2: warning: w [c]"})
        self.assertEqual(tidy.findings(""), [])
        self.assertEqual(tidy.findings("12 warnings generated.\n"), [], "a line before any diagnostic is none")


class PlainPathsTest(unittest.TestCase):
    def test_every_path_of_the_fixes_is_made_plain_and_nothing_else_moves(self):
        fixes = (
            "MainSourceFile:  '/a/apps/doc/manager.cpp'\n"
            "Diagnostics:\n"
            "  - DiagnosticMessage:\n"
            "      FilePath:        '/a/apps/doc/../doc/manager.hpp'\n"
            "      Replacements:\n"
            "        - FilePath:        '/a/apps/x/../doc/it''s.hpp'\n"
            "          ReplacementText: ' override'\n"
        )
        self.assertEqual(tidy.plain_paths(fixes),
                         fixes.replace("/a/apps/doc/../doc/manager.hpp", native("/a/apps/doc/manager.hpp"))
                         .replace("/a/apps/x/../doc/it''s.hpp", native("/a/apps/doc/it''s.hpp")))
        self.assertEqual(tidy.plain_paths("FilePath: '/a/b.hpp'\n"), f"FilePath: '{native('/a/b.hpp')}'\n")


class FixTest(unittest.TestCase):
    def test_each_source_writes_its_fixes_to_a_file_of_its_own_though_their_objects_share_a_name(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        one = ROOT / "apps" / "one" / "main.cpp"
        two = ROOT / "apps" / "two" / "main.cpp"
        (folder / "compile_commands.json").write_text(json.dumps([
            {"directory": str(folder), "file": str(one), "output": "apps/one/main.cpp.o", "command": "c++"},
            {"directory": str(folder), "file": str(two), "output": "apps/two/main.cpp.o", "command": "c++"},
        ]))
        build = mock.Mock(dir=folder)
        written: list[Path | None] = []

        def run(_tidy: Path, _build: object, _source: Source, fixes: Path | None) -> subprocess.CompletedProcess:
            written.append(fixes)
            return subprocess.CompletedProcess([], 0, "", "")

        with (mock.patch.object(tidy, "_run", side_effect=run),
              mock.patch.object(tidy.deps, "clang_tidy", return_value=Path("clang-tidy")),
              mock.patch.object(tidy.deps, "clang_apply_replacements", return_value=Path("apply")),
              mock.patch.object(tidy.subprocess, "run") as applied,
              mock.patch.object(tidy.formatting, "changed_files", return_value=[]),
              mock.patch.object(tidy.formatting, "format_tree") as formatted,
              contextlib.redirect_stdout(io.StringIO())):
            self.assertEqual(tidy.tidy(build, ["--fix"]), 0)
        self.assertEqual(len(written), 2)
        self.assertEqual(len(set(written)), 2, "two sources wrote their fixes to one file")
        scratch = {path.parent for path in written if path is not None}
        self.assertEqual(len(scratch), 1)
        applied.assert_called_once_with([Path("apply"), str(scratch.pop())], cwd=ROOT, check=True)
        formatted.assert_called_once_with([])

        written.clear()
        with mock.patch.object(tidy, "_run", side_effect=run), \
                mock.patch.object(tidy.deps, "clang_tidy", return_value=Path("clang-tidy")), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tidy.tidy(build, []), 0)
        self.assertEqual(written, [None, None], "a run that fixes nothing exports nothing")
