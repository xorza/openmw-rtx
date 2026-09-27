import re
import unittest

from omw.main import HARNESS_VERBS, USAGE, Line, parse
from omw.system import ROOT, Refusal
from omw.testing import ctest_arguments


class ParseTest(unittest.TestCase):
    def test_a_line_is_the_flavour_the_verb_and_the_rest(self):
        cases = [
            (["test"], Line("debug", "test", [])),
            (["release", "bench", "--seconds=3"], Line("release", "bench", ["--seconds=3"])),
            (["asan", "test", "--all"], Line("asan", "test", ["--all"])),
            (["archive", "v1"], Line("package", "archive", ["v1"])),
            (["package", "archive"], Line("package", "archive", [])),
            (["profile", "--offcpu"], Line("release", "profile", ["--offcpu"])),
            (["crash", "a.dmp"], Line(None, "crash", ["a.dmp"])),
            (["format"], Line(None, "format", [])),
            (["plain", "exec", "ls", "-l"], Line("plain", "exec", ["ls", "-l"])),
        ]
        for argv, expected in cases:
            with self.subTest(argv=argv):
                self.assertEqual(parse(argv), expected)

    def test_a_line_that_says_nothing_or_contradicts_itself_is_refused(self):
        cases = [
            ([], USAGE),
            (["debug"], USAGE),
            (["help"], USAGE),
            (["debug", "crash", "a.dmp"], "crash is not made of a build, so it takes no flavour"),
            (["release", "archive"], "archive is made of the package flavour: `omw archive`"),
            (["debug", "--views=x"], "name a verb before the switches: `omw debug view --views=x`"),
        ]
        for argv, message in cases:
            with self.subTest(argv=argv):
                with self.assertRaises(Refusal) as refused:
                    parse(argv)
                self.assertEqual(str(refused.exception), message)

    def test_a_word_that_is_no_verb_is_refused_before_anything_is_built(self):
        for argv in (["debgu", "test"], ["debug", "tets"]):
            with self.subTest(argv=argv):
                with self.assertRaises(Refusal) as refused:
                    parse(argv)
                self.assertIn(f"no verb or flavour is called {argv[0] if argv[0] == 'debgu' else argv[1]!r}",
                              str(refused.exception))

    def test_the_harness_verbs_are_the_harness_own(self):
        text = (ROOT / "apps" / "rtxtool" / "verbs.cpp").read_text()
        named = re.findall(r'std::pair\{ Verbs::\w+, std::string_view\("(\w+)"\) \}', text)
        self.assertEqual(tuple(named), HARNESS_VERBS)


class CtestArgumentsTest(unittest.TestCase):
    def test_the_switches_become_labels_and_the_rest_passes(self):
        cases = [
            ("debug", [], ["-L", "fork"]),
            ("debug", ["--all"], []),
            ("debug", ["--without-device"], ["-LE", "device", "-L", "fork"]),
            ("asan", ["--without-device", "--all", "-R", "crash"], ["-LE", "device", "-R", "crash"]),
            ("plain", [], []),
        ]
        for flavour, args, expected in cases:
            with self.subTest(flavour=flavour, args=args):
                self.assertEqual(ctest_arguments(flavour, args), expected)


if __name__ == "__main__":
    unittest.main()
