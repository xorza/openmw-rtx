import contextlib
import io
import re
import unittest

from omw import deps, gate
from omw.main import BUILD_VERBS, BUILDLESS_VERBS, HARNESS_VERBS, USAGE, Line, main, parse
from omw.system import ROOT, Refusal, read_text, status


class ParseTest(unittest.TestCase):
    def test_a_line_is_the_flavour_the_verb_and_the_rest(self):
        cases = [
            (["test"], Line("debug", "test", [])),
            (["release", "bench", "--seconds=3"], Line("release", "bench", ["--seconds=3"])),
            (["asan", "test", "--all"], Line("asan", "test", ["--all"])),
            (["tsan", "test", "--without-device"], Line("tsan", "test", ["--without-device"])),
            (["archive", "v1"], Line("release", "archive", ["v1"])),
            (["release", "archive"], Line("release", "archive", [])),
            (["profile", "--offcpu"], Line("release", "profile", ["--offcpu"])),
            (["crash", "a.dmp"], Line(None, "crash", ["a.dmp"])),
            (["format"], Line(None, "format", [])),
            (["help"], Line(None, "help", [])),
            (["--help"], Line(None, "help", [])),
            (["release", "-h"], Line(None, "help", [])),
            (["format", "--check"], Line(None, "format", ["--check"])),
            (["asan", "exec", "ls", "-l"], Line("asan", "exec", ["ls", "-l"])),
        ]
        for argv, expected in cases:
            with self.subTest(argv=argv):
                self.assertEqual(parse(argv), expected)

    def test_help_asked_for_succeeds_and_a_child_s_death_is_the_shell_s_status(self):
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            self.assertEqual(main(["help"]), 0)
        self.assertEqual(printed.getvalue(), USAGE + "\n")
        # SIGSEGV is signal 11, which Python reports as -11 and a shell as 128 + 11.
        self.assertEqual(status(-11), 139)
        self.assertEqual(status(-2), 130)
        self.assertEqual(status(0), 0)
        self.assertEqual(status(3), 3)

    def test_a_line_that_says_nothing_or_contradicts_itself_is_refused(self):
        cases = [
            ([], USAGE),
            (["debug"], USAGE),
            (["debug", "crash", "a.dmp"], "crash is not made of a build, so it takes no flavour"),
            (["debug", "archive"], "archive is made of the release flavour: `omw archive`"),
            (["debug", "--views=x"], "name a verb before the switches: `omw debug view --views=x`"),
            (["build", "release"], "the flavour comes before the verb: `omw release build`"),
            (["debug", "test", "asan", "--all"], "the flavour comes before the verb: `omw asan test --all`"),
            (["profile", "release"], "the flavour comes before the verb: `omw release profile`"),
            (["format", "debug"], "format is not made of a build, so it takes no flavour"),
        ]
        for argv, message in cases:
            with self.subTest(argv=argv):
                with self.assertRaises(Refusal) as refused:
                    parse(argv)
                self.assertEqual(str(refused.exception), message)

    def test_a_word_that_is_no_verb_is_refused_before_anything_is_built(self):
        for argv, word in ((["debgu", "test"], "debgu"), (["debug", "tets"], "tets")):
            with self.subTest(argv=argv):
                with self.assertRaises(Refusal) as refused:
                    parse(argv)
                self.assertIn(f"no verb or flavour is called {word!r}", str(refused.exception))

    def test_bootstrap_prints_the_cached_tools_one_a_line_from_the_root_and_takes_no_other_word(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(main(["bootstrap", "--cached-tools"]), 0)
        self.assertEqual(out.getvalue().splitlines(),
                         [folder.relative_to(ROOT).as_posix() for folder in deps.cached_tools()])
        self.assertTrue(all(line.startswith("deps/") for line in out.getvalue().splitlines()))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertNotEqual(main(["bootstrap", "--tools"]), 0)
        self.assertEqual(err.getvalue(), "omw: bootstrap takes no arguments but --cached-tools\n")

    def test_the_usage_names_every_verb(self):
        for verb in [*BUILD_VERBS, *HARNESS_VERBS, *BUILDLESS_VERBS]:
            with self.subTest(verb=verb):
                self.assertRegex(USAGE, rf"(?m)^  (\w+, )*{verb}\b")

    def test_the_gate_is_the_debug_flavour_s_and_says_its_steps_once(self):
        # The flavour CI builds and tests, and so no other, before anything builds.
        self.assertEqual(parse(["gate"]), Line("debug", "gate", []))
        for flavour in ("release", "asan", "tsan"):
            with self.subTest(flavour=flavour), self.assertRaisesRegex(Refusal, r"gate is made of the debug flavour"):
                parse([flavour, "gate"])
        # The old flavours are no flavours at all.
        for word in ("full", "package"):
            with self.subTest(word=word), self.assertRaises(Refusal):
                parse([word, "build"])

        # The help's line is `gate.SUMMARY`, wrapped.
        said = " ".join(re.search(r"(?ms)^  gate +(.*?)\s+—\s+stops", USAGE).group(1).split())
        self.assertEqual(said, gate.SUMMARY)

    def test_the_harness_verbs_are_the_harness_own(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "verbs.cpp")
        named = re.findall(r'std::pair\{ Verbs::\w+, std::string_view\("(\w+)"\) \}', text)
        self.assertEqual(tuple(named), HARNESS_VERBS)


if __name__ == "__main__":
    unittest.main()
