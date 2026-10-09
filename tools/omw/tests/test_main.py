import contextlib
import io
import re
import unittest

from omw import gate
from omw.main import BUILD_VERBS, BUILDLESS_VERBS, HARNESS_VERBS, USAGE, Line, main, parse
from omw.system import FORK, ROOT, Refusal, read_text, status
from omw.testing import ctest_arguments


class ParseTest(unittest.TestCase):
    def test_a_line_is_the_flavour_the_verb_and_the_rest(self):
        cases = [
            (["test"], Line("debug", "test", [])),
            (["release", "bench", "--seconds=3"], Line("release", "bench", ["--seconds=3"])),
            (["asan", "test", "--all"], Line("asan", "test", ["--all"])),
            (["tsan", "test", "--without-device"], Line("tsan", "test", ["--without-device"])),
            (["archive", "v1"], Line("package", "archive", ["v1"])),
            (["package", "archive"], Line("package", "archive", [])),
            (["profile", "--offcpu"], Line("release", "profile", ["--offcpu"])),
            (["crash", "a.dmp"], Line(None, "crash", ["a.dmp"])),
            (["format"], Line(None, "format", [])),
            (["help"], Line(None, "help", [])),
            (["--help"], Line(None, "help", [])),
            (["release", "-h"], Line(None, "help", [])),
            (["format", "--check"], Line(None, "format", ["--check"])),
            (["full", "exec", "ls", "-l"], Line("full", "exec", ["ls", "-l"])),
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
            (["release", "archive"], "archive is made of the package flavour: `omw archive`"),
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

    def test_the_usage_names_every_verb(self):
        for verb in [*BUILD_VERBS, *HARNESS_VERBS, *BUILDLESS_VERBS]:
            with self.subTest(verb=verb):
                self.assertRegex(USAGE, rf"(?m)^  (\w+, )*{verb}\b")

    def test_the_gate_is_the_full_flavour_s_and_says_its_steps_once(self):
        # Every program CI builds, and so no other flavour, before anything builds.
        self.assertEqual(parse(["gate"]), Line("full", "gate", []))
        for flavour in ("debug", "release", "package"):
            with self.subTest(flavour=flavour), self.assertRaisesRegex(Refusal, r"gate is made of the full flavour"):
                parse([flavour, "gate"])

        # The help's line is `gate.STEPS`, wrapped.
        said = " ".join(re.search(r"(?ms)^  gate +(.*?)\s+—\s+stops", USAGE).group(1).split())
        self.assertEqual(said, gate.STEPS)

    def test_every_fork_folder_is_a_folder(self):
        for folder in FORK:
            with self.subTest(folder=folder):
                self.assertTrue((ROOT / folder).is_dir())

    def test_the_harness_verbs_are_the_harness_own(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "verbs.cpp")
        named = re.findall(r'std::pair\{ Verbs::\w+, std::string_view\("(\w+)"\) \}', text)
        self.assertEqual(tuple(named), HARNESS_VERBS)


class CtestArgumentsTest(unittest.TestCase):
    def test_the_device_switch_becomes_a_label_and_the_rest_passes(self):
        cases = [
            ([], ["--parallel"]),
            (["--without-device"], ["--parallel", "-LE", "device"]),
            (["--without-device", "-R", "crash"], ["--parallel", "-LE", "device", "-R", "crash"]),
        ]
        for args, expected in cases:
            with self.subTest(args=args):
                self.assertEqual(ctest_arguments(args), expected)


if __name__ == "__main__":
    unittest.main()
