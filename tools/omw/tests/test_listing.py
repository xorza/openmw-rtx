import unittest

from omw.listing import unlisted


class UnlistedTest(unittest.TestCase):
    def test_a_source_no_build_compiles_is_named_unless_a_rule_excuses_it(self):
        tracked = [
            "apps/openmw/engine.cpp",
            "apps/openmw/forgotten.cpp",
            "apps/openmw/androidmain.cpp",
            "apps/opencs/main.cpp",
            "components/misc/rng.cpp",
            "components/misc/forgotten.cpp",
            "components/misc/rng.hpp",
            "components/platform/processposix.cpp",
            "components/platform/processwin32.cpp",
            "components/platform/filestdio.cpp",
            "components/toutf8/geniconv.cpp",
            "components/crashcatcher/crashunsupported.cpp",
            "apps/wizard/installationpage.cpp",
            "extern/oics/tinyxml.cpp",
        ]
        # The CS compiled nothing, so its program is off in this flavour; a header is no source; a
        # file of another system, Android's entry, the generator and the other catcher are excused.
        # The system decides which half of a pair is the other's: each build compiles its own.
        # The wizard's unshield half is Windows's to leave out, and every other system's to build.
        forgotten = ["apps/openmw/forgotten.cpp", "components/misc/forgotten.cpp"]
        wizard = "apps/wizard/installationpage.cpp"
        for windows, own in ((False, "components/platform/processposix.cpp"),
                             (True, "components/platform/processwin32.cpp")):
            with self.subTest(windows=windows):
                compiled = {"apps/openmw/engine.cpp", "apps/wizard/main.cpp", "components/misc/rng.cpp", own}
                unshield = [] if windows else [wizard]
                self.assertEqual(unlisted(tracked, compiled, windows), sorted([*forgotten, *unshield]))
                self.assertEqual(unlisted(tracked, compiled - {own}, windows), sorted([*forgotten, *unshield, own]))

if __name__ == "__main__":
    unittest.main()
