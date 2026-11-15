import contextlib
import io
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import lint
from omw.lint import misnamed, unguarded
from omw.system import Refusal


class LintTest(unittest.TestCase):
    def test_a_source_is_named_in_lower_case_and_digits_in_a_folder(self):
        names = ["apps/a/foo2.cpp", "apps/a/FooBar.hpp", "apps/a/foo_bar.h", "apps/a/foo.h", "top.cpp"]
        self.assertEqual([line.split(":")[0] for line in misnamed(names)],
                         ["apps/a/FooBar.hpp", "apps/a/foo_bar.h", "top.cpp"])

    def test_a_header_opens_with_pragma_once_and_nothing_else_counts(self):
        headers = {
            "a.hpp": "#pragma once\n\n#include <vector>\n",
            "b.hpp": "// The licence.\n/* More of it. */\n#pragma once\n",
            "c.hpp": "#ifndef C_HPP\n#define C_HPP\n#endif\n",
            "d.hpp": "#include <vector>\n#pragma once\n",
            "e.hpp": "// Comments alone.\n",
        }
        self.assertEqual([line.split(":")[0] for line in unguarded(headers)], ["c.hpp", "d.hpp", "e.hpp"])
        self.assertIn("#ifndef C_HPP", unguarded({"c.hpp": headers["c.hpp"]})[0])

    def test_the_driver_is_linted_and_typed_for_each_system_by_the_fetched_tools_and_a_failure_names_its_tool(self):
        ran: list[tuple[list[str], dict[str, str] | None]] = []

        def run(command: list[str], cwd: Path, env: dict[str, str] | None, check: bool) -> mock.Mock:
            self.assertEqual(cwd, lint.ROOT)
            self.assertFalse(check)
            ran.append((command, env))
            # The Windows type check fails, and the others pass.
            return mock.Mock(returncode=3 if "win32" in command else 0)

        with (mock.patch.object(lint.deps, "ruff", return_value=Path("fetched/ruff")),
              mock.patch.object(lint.deps, "mypy", return_value=["python", "-c", "mypy"]),
              mock.patch.object(lint.deps, "zizmor", return_value=Path("fetched/zizmor")),
              mock.patch.object(lint, "_containers", return_value=[]),
              mock.patch.object(lint.subprocess, "run", side_effect=run)):
            self.assertEqual(lint._driver(), ["mypy for win32 exited with 3"])
            self.assertEqual(lint._workflows(), [])
        typed = ["--python-version", "3.11", "--platform"]
        cache = lint.OUT / "mypy"
        self.assertEqual([command for command, _ in ran], [
            [str(Path("fetched/ruff")), "check", "--config", "tools/omw/ruff.toml", "tools/omw", "omw"],
            # A cache a system, so neither run checks again what the other left.
            ["python", "-c", "mypy", *typed, "linux", "--always-false", "WINDOWS", "--cache-dir", str(cache / "linux"),
             "-p", "omw"],
            ["python", "-c", "mypy", *typed, "win32", "--always-true", "WINDOWS", "--cache-dir", str(cache / "win32"),
             "-p", "omw"],
            [str(Path("fetched/zizmor")), "--offline", "."],
        ])
        # mypy finds the driver's package by its own variable, and the other tools take the environment as it is.
        self.assertEqual([None if env is None else env["MYPYPATH"] for _, env in ran],
                         [None, str(lint.ROOT / "tools"), str(lint.ROOT / "tools"), None])

    def test_every_container_a_workflow_names_is_the_release_base_s_image(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root)
        (root / "env").mkdir()
        (root / "env" / "release-base.Dockerfile").write_text("# base\nFROM almalinux:8@sha256:aa\nRUN true\n")
        workflows = root / ".github" / "workflows"
        workflows.mkdir(parents=True)
        (workflows / "ci.yml").write_text(
            "jobs:\n"
            "  a:\n"
            "    container: ${{ matrix.container }}\n"
            "    strategy:\n"
            "      matrix:\n"
            "        include:\n"
            "          - container: almalinux:8@sha256:aa\n"
            "          - container: almalinux:8\n")
        (workflows / "deps.yml").write_text("jobs:\n  b:\n    container: almalinux:8@sha256:bb\n")
        # The bare tag and another digest are found; the expression and the matching entry are not.
        wanted = "is not env/release-base.Dockerfile's almalinux:8@sha256:aa"
        self.assertEqual(lint._containers(root), [
            f".github/workflows/ci.yml:8: container almalinux:8 {wanted}",
            f".github/workflows/deps.yml:3: container almalinux:8@sha256:bb {wanted}",
        ])

        # The tree's own workflows hold.
        self.assertEqual(lint._containers(lint.ROOT), [])

    def test_lupdate_is_the_vcpkg_sets_and_nothing_on_the_path_or_in_the_environment(self):
        with mock.patch.object(lint.deps, "set_tool", return_value=Path("set/lupdate")) as tool, \
                mock.patch.dict(lint.os.environ, {"LUPDATE": "elsewhere/lupdate"}), \
                mock.patch("omw.system.on_path", return_value="path/lupdate"):
            self.assertEqual(lint._lupdate(), str(Path("set/lupdate")))
        tool.assert_called_once_with("Qt6/bin", "lupdate")

    def test_translations_regenerates_every_ts_file_and_purges_only_when_asked(self):
        with mock.patch.object(lint, "_lupdate", return_value="lupdate"), \
                mock.patch.object(lint.subprocess, "run") as ran:
            self.assertEqual(lint.translations([]), 0)
            kept = [call.args[0] for call in ran.call_args_list]
            ran.reset_mock()
            self.assertEqual(lint.translations(["--purge"]), 0)
            purged = [call.args[0] for call in ran.call_args_list]
        # One lupdate a group, over the group's sources into every one of its .ts files.
        self.assertEqual(len(kept), len(lint.TRANSLATIONS))
        for command, (sources, pattern) in zip(kept, lint.TRANSLATIONS, strict=True):
            ts = sorted(str(path) for path in (lint.ROOT / "files" / "lang").glob(pattern))
            self.assertTrue(ts, pattern)
            self.assertEqual(command, ["lupdate", "-silent", "-locations", "none", *sources, "-ts", *ts])
        # --purge adds -no-obsolete and changes nothing else.
        self.assertEqual(purged, [[*command[:4], "-no-obsolete", *command[4:]] for command in kept])
        with self.assertRaisesRegex(Refusal, "--purge"):
            lint.translations(["--prune"])

    def test_a_check_nobody_has_is_refused_before_any_runs_and_a_refusal_leaves_the_findings_before_it(self):
        with self.assertRaisesRegex(Refusal, "no check is called style"):
            lint.lint(["names", "style"])

        def refused() -> list[str]:
            raise Refusal("zizmor 1.30.1, the version CI pins, is not on the PATH")

        checks = {"first": lambda: ["a.cmake:2: indented with a tab"], "second": refused}
        printed = io.StringIO()
        with mock.patch.dict(lint.CHECKS, checks, clear=True), contextlib.redirect_stdout(printed), \
                self.assertRaises(Refusal):
            lint.lint([])
        self.assertEqual(printed.getvalue(), "a.cmake:2: indented with a tab\n")


if __name__ == "__main__":
    unittest.main()
