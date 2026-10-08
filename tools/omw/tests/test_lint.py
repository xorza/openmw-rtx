import contextlib
import io
import unittest
from unittest import mock

from omw import lint
from omw.lint import Pinned, command_for, misnamed, tabbed, version_of
from omw.system import Refusal


class LintTest(unittest.TestCase):
    def test_a_tab_that_indents_is_named_and_one_inside_a_line_is_not(self):
        text = "set(A 1)\n\tset(B 2)\n  \tset(C 3)\nset(D\t4)\n"
        self.assertEqual([line.split(":")[1] for line in tabbed("x.cmake", text)], ["2", "3"])

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

        with mock.patch.object(lint, "on_path", return_value=None), self.assertRaisesRegex(Refusal, "is not on the PATH"):
            command_for(tool, ci=False)

    def test_a_check_nobody_has_is_refused_before_any_runs_and_a_refusal_leaves_the_findings_before_it(self):
        with self.assertRaisesRegex(Refusal, "no check is called style"):
            lint.lint(["cmake", "style"])

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
