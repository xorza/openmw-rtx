import contextlib
import io
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import lint
from omw.lint import Pinned, command_for, misnamed, version_of
from omw.system import Refusal


class LintTest(unittest.TestCase):
    def test_a_source_is_named_in_lower_case_and_digits_in_a_folder(self):
        names = ["apps/a/foo2.cpp", "apps/a/FooBar.hpp", "apps/a/foo_bar.h", "apps/a/foo.h", "top.cpp"]
        self.assertEqual([line.split(":")[0] for line in misnamed(names)],
                         ["apps/a/FooBar.hpp", "apps/a/foo_bar.h", "top.cpp"])

    def test_the_version_is_the_second_word_of_what_a_tool_says(self):
        self.assertEqual(version_of("ruff 0.16.9\n"), "0.16.9")
        self.assertEqual(version_of("mypy 2.3.1 (compiled: yes)"), "2.3.1")
        self.assertIsNone(version_of(""))

    def test_the_pin_runs_from_the_path_through_pipx_on_ci_and_is_refused_otherwise(self):
        tool = Pinned("ruff", "0.16.9")
        said = mock.Mock(stdout="ruff 0.16.9\n")
        other = mock.Mock(stdout="ruff 0.15.0\n")

        def found(program: str) -> str | None:
            return {"ruff": "/bin/ruff", "pipx": "/bin/pipx"}.get(program)

        with mock.patch.object(lint, "on_path", side_effect=found), \
                mock.patch("omw.system.on_path", side_effect=found):
            with mock.patch.object(lint.subprocess, "run", return_value=said):
                self.assertEqual(command_for(tool, ci=False), ["/bin/ruff"])
            with mock.patch.object(lint.subprocess, "run", return_value=other):
                self.assertEqual(command_for(tool, ci=True), ["/bin/pipx", "run", "ruff==0.16.9"])
                with self.assertRaisesRegex(Refusal, r"on the PATH is ruff 0\.15\.0: `pipx install ruff==0\.16\.9`"):
                    command_for(tool, ci=False)

        with (mock.patch.object(lint, "on_path", return_value=None),
              self.assertRaisesRegex(Refusal, "is not on the PATH")):
            command_for(tool, ci=False)

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
                mock.patch.object(lint, "on_path", return_value="path/lupdate"):
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
