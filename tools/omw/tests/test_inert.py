import contextlib
import io
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import inert
from omw.inert import BODY, REMOVED, TRUNCATED, cut, neutral

REPO = "o/r"


class NeutralTest(unittest.TestCase):
    def test_mentions_references_numbers_and_commands_are_inert(self):
        self.assertEqual(neutral("ask @someone now", REPO, 100), "ask ` @someone ` now")
        self.assertEqual(neutral("mail a@b.com", REPO, 100), "mail a@b.com")
        self.assertEqual(neutral("see other/repo#12 and #12", REPO, 100), "see ` other/repo#12 ` and ` #12 `")
        self.assertEqual(neutral("/approve\nnot /here", REPO, 100), "` /approve `\nnot /here")
        self.assertEqual(neutral("<img src=x onerror=y>", REPO, 100), "&lt;img src=x onerror=y>")
        self.assertEqual(neutral("a\x00b​c", REPO, 100), "abc")

    def test_upstream_s_numbers_link_nothing_of_this_repository(self):
        self.assertEqual(neutral("Fixes #12, see GH-3 and `#4`", REPO, 100), "Fixes ` #12 `, see ` GH-3 ` and `#4`")
        fenced = "```\nabc123 Fix #7\n```"
        untouched = f"# 12 trees\n&#35; a#1\n{fenced}"
        self.assertEqual(neutral(untouched, REPO, 100), untouched)
        # A number spelled around an invisible character is one once the character is out.
        self.assertEqual(neutral("Closes #​12", REPO, 100), "Closes ` #12 `")
        # A cross-reference's span stays whole though a number stands in it.
        self.assertEqual(neutral("see a/b.#1 and x/y#2", REPO, 100), "see a/b.` #1 ` and ` x/y#2 `")

    def test_code_stays_as_written(self):
        self.assertEqual(neutral("`@x <b>` and @y", REPO, 100), "`@x <b>` and ` @y `")
        fenced = "```\n@x <b> /cmd #3\n```"
        self.assertEqual(neutral(fenced, REPO, 100), fenced)

    def test_links(self):
        self.assertEqual(neutral("[a](javascript:alert(1))", REPO, 100), f"[a]({REMOVED}))")
        self.assertEqual(neutral("http://example.org/x", REPO, 100), REMOVED)
        # Any host over HTTPS stands: a report cites wherever a source is.
        self.assertEqual(neutral("https://example.org/paper.pdf", REPO, 100), "https://example.org/paper.pdf")
        self.assertEqual(neutral("https://github.com/o/r/issues/3", REPO, 100), "https://github.com/o/r/issues/3")
        self.assertEqual(neutral("https://github.com/x/y/issues/3", REPO, 100), "` https://github.com/x/y/issues/3 `")
        self.assertEqual(neutral("https://github.com/x/y/blob/main/a.cpp", REPO, 100),
                         "https://github.com/x/y/blob/main/a.cpp")

    def test_the_verb_prints_a_report_made_safe_to_post_and_cut_to_a_body(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        report = folder / "report.md"
        fenced = "```\nabc123 Fix #7 by @her\n```"
        written = f"Closes #12 for @him, s​ecret, <b>\n{fenced}\n"
        # A byte that is no UTF-8, as an agent's file may hold, and more than a body takes.
        report.write_bytes(written.encode() + b"\xff" + b"x" * BODY)
        said = io.TextIOWrapper(io.BytesIO(), encoding="utf-8")
        with mock.patch.dict(inert.os.environ, {"GITHUB_REPOSITORY": REPO}), contextlib.redirect_stdout(said):
            self.assertEqual(inert.inert([str(report)]), 0)
        said.flush()
        text = said.buffer.getvalue().decode()
        self.assertTrue(text.startswith(f"Closes ` #12 ` for ` @him `, secret, &lt;b>\n{fenced}\n"), text[:120])
        self.assertTrue(text.endswith(TRUNCATED))
        self.assertLessEqual(len(text), BODY)

    def test_cut_bounds_and_closes(self):
        # A fence opened and never closed would swallow the template's footer into its block.
        self.assertEqual(cut("text\n```\ncode", 100), "text\n```\ncode\n```")
        self.assertEqual(cut("text\n~~~\ncode", 100), "text\n~~~\ncode\n~~~")
        long = "x" * 200
        done = cut(long, 100)
        self.assertLessEqual(len(done), 100)
        self.assertEqual(done, "x" * (100 - len(TRUNCATED) - 4) + TRUNCATED)
        self.assertEqual(cut("x" * 100, 100), "x" * 100)
