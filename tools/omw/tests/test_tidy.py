import contextlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from dataclasses import dataclass
from pathlib import Path
from unittest import mock

from omw import tidy
from omw.system import ROOT
from omw.tidy import Source
from omw.tidycache import Compile

BUILD = ROOT / "out" / "debug"
# How `entry` compiles every source.
COMPILE = Compile(str(BUILD), "clang++ -c")


def native(path: str) -> str:
    """`path` as the system spells a plain path: `os.path.normpath` gives Windows its own separator."""
    return path.replace("/", os.sep)


def written(folder: Path, name: str, text: str) -> Path:
    """`text` into `name`, then the file system's clock past its change time, which the store keeps no source of a
    read changed at or after a run's start for; a coarse clock tick is milliseconds."""
    path = folder / name
    path.write_text(text, encoding="utf-8")
    probe = folder / "probe"
    for _ in range(100_000):
        probe.touch()
        if probe.stat().st_mtime_ns > path.stat().st_ctime_ns:
            return path
    raise AssertionError("the file system's clock stood still")


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
                         [Source(app, BUILD / "apps/main.o", COMPILE),
                          Source(component, BUILD / "components/a.o", COMPILE)])


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

        a = Source(ROOT / "apps/a.cpp", BUILD / "apps/a.o", COMPILE)
        b = Source(ROOT / "apps/b.cpp", BUILD / "apps/b.o", COMPILE)
        c = Source(ROOT / "apps/c.cpp", BUILD / "apps/c.o", COMPILE)
        never_built = Source(ROOT / "apps/d.cpp", BUILD / "apps/d.o", COMPILE)
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

        def run(_tidy: Path, _build: object, _source: Source, fixes: Path | None,
                _dependencies: Path) -> subprocess.CompletedProcess:
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
        # The whole tree's lint builds what its sources wait on alone, and none of their objects.
        build.build_generated.assert_called_once_with({one, two})
        build.build_default.assert_not_called()
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

        # A file named is linted with what includes it, which Ninja's log of a whole build says.
        written.clear()
        build.reset_mock()
        with mock.patch.object(tidy, "_run", side_effect=run), \
                mock.patch.object(tidy.deps, "clang_tidy", return_value=Path("clang-tidy")), \
                mock.patch.object(tidy, "output", return_value="") as deps_log, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tidy.tidy(build, [str(one)]), 0)
        build.build_default.assert_called_once_with()
        build.build_generated.assert_not_called()
        self.assertEqual(deps_log.call_args.args[0][-2:], ["-t", "deps"])


class RememberedTest(unittest.TestCase):
    def test_a_clean_run_is_not_linted_again_until_a_read_changes_and_a_run_with_findings_always_is(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)

        def write(name: str, text: str) -> Path:
            return written(folder, name, text)

        clean = write("clean.cpp", "int a;")
        header = write("clean.hpp", "#pragma once")
        dirty = write("dirty.cpp", "int b;")
        silent = write("silent.cpp", "int c;")
        (folder / "compile_commands.json").write_text(json.dumps([
            {"directory": str(folder), "file": str(path), "output": f"{path.stem}.o", "command": f"c++ -c {path.name}"}
            for path in (clean, dirty, silent)
        ]))
        build = mock.Mock(dir=folder)
        linted: list[Path] = []

        def run(_tidy: Path, _build: object, source: Source, _fixes: Path | None,
                dependencies: Path) -> subprocess.CompletedProcess:
            linted.append(source.file)
            # A run that writes no dependency file, as one whose flag its driver ignored would.
            if source.file != silent:
                dependencies.write_text(f"{source.file.stem}.o: {source.file.name} {header.name}\n", encoding="utf-8")
            said = f"{dirty}:1:1: warning: w [c]\n" if source.file == dirty else ""
            return subprocess.CompletedProcess([], 0, said, "")

        def tidy_run() -> str:
            linted.clear()
            said = io.StringIO()
            with (mock.patch.object(tidy, "LINTED", (folder,)), mock.patch.object(tidy, "_run", side_effect=run),
                  mock.patch.object(tidy.deps, "clang_tidy", return_value=Path("clang-tidy")),
                  contextlib.redirect_stdout(said)):
                self.assertEqual(tidy.tidy(build, []), 1)
            return said.getvalue()

        self.assertIn("tidy: 1 findings over 3 sources, 0 of them unchanged since a clean run", tidy_run())
        self.assertEqual(sorted(linted), [clean, dirty, silent])
        said = tidy_run()
        self.assertEqual(sorted(linted), [dirty, silent], "the clean run stands, and the others are linted again")
        self.assertIn("tidy: 1 findings over 3 sources, 1 of them unchanged since a clean run", said)
        self.assertIn(f"{dirty}:1:1: warning: w [c]", said, "a finding is said again on every run")

        write("clean.hpp", "#pragma once // edited")
        said = tidy_run()
        self.assertEqual(sorted(linted), [clean, dirty, silent], "a read edited")
        self.assertIn(f"tidy: 1 remembered runs stand no more, by {header} (1)\n", said, "and named")
        tidy_run()
        self.assertEqual(sorted(linted), [dirty, silent])
        # A source the database no longer holds is dropped from the store.
        (folder / "compile_commands.json").write_text(json.dumps([
            {"directory": str(folder), "file": str(dirty), "output": "dirty.o", "command": "c++ -c dirty.cpp"}]))
        tidy_run()
        self.assertEqual(tidy.tidycache.load(folder / "tidy-clean.json"), {})

    def test_a_run_stopped_partway_keeps_in_the_store_what_came_out_clean_before_it_stopped(self):
        """A cancel stops CI's lint partway. **Each checkpoint writes what came out clean so far**, which a run
        killed outright keeps, and **a run stopped by an exception writes it on its way out**, between checkpoints
        too. One hand, so the order is the list's; the third source stops the run."""
        for checkpoint_s, at_the_stop in ((0.0, ["a.cpp", "b.cpp"]), (float("inf"), [])):
            with self.subTest(checkpoint_s=checkpoint_s):
                stopped = self.stopped_at_the_third(checkpoint_s, at_the_stop)
                self.assertEqual(stopped.at_the_stop, at_the_stop, "the store when the run stopped")
                self.assertEqual(stopped.left, ["a.cpp", "b.cpp"], "the store the stopped run left")

    @dataclass
    class Stopped:
        at_the_stop: list[str]
        left: list[str]

    def stopped_at_the_third(self, checkpoint_s: float, awaited: list[str]) -> "RememberedTest.Stopped":
        """Four clean sources linted by one hand, the third interrupted, with checkpoints every `checkpoint_s`: the
        sources the store held as the third began, after a bounded wait for `awaited`, and those it held after."""
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        paths = [written(folder, name, "int a;") for name in ("a.cpp", "b.cpp", "c.cpp", "d.cpp")]
        (folder / "compile_commands.json").write_text(json.dumps([
            {"directory": str(folder), "file": str(path), "output": f"{path.stem}.o", "command": f"c++ -c {path.name}"}
            for path in paths
        ]))
        store = folder / "tidy-clean.json"
        at_the_stop: list[str] = []

        def names() -> list[str]:
            return sorted(Path(source).name for source in tidy.tidycache.load(store))

        def run(_tidy: Path, _build: object, source: Source, _fixes: Path | None,
                dependencies: Path) -> subprocess.CompletedProcess:
            if source.file == paths[2]:
                # The driver takes in the second source's run while this one starts: a bounded wait for the
                # checkpoint that follows it, where there is one.
                for _ in range(1000):
                    if names() == awaited:
                        break
                    time.sleep(0.005)
                at_the_stop.extend(names())
                raise KeyboardInterrupt
            dependencies.write_text(f"{source.file.stem}.o: {source.file.name}\n", encoding="utf-8")
            return subprocess.CompletedProcess([], 0, "", "")

        with (mock.patch.object(tidy, "LINTED", (folder,)), mock.patch.object(tidy, "_run", side_effect=run),
              mock.patch.object(tidy, "CHECKPOINT_S", checkpoint_s), mock.patch.object(tidy, "jobs", return_value=1),
              mock.patch.object(tidy.deps, "clang_tidy", return_value=Path("clang-tidy")),
              contextlib.redirect_stdout(io.StringIO()), self.assertRaises(KeyboardInterrupt)):
            tidy.tidy(mock.Mock(dir=folder), [])
        return RememberedTest.Stopped(at_the_stop, names())
