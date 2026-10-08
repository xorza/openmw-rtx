import unittest

from omw.listing import qt_guarded_sources, qt_sources, unlisted


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
            "apps/openmw/countslinux.cpp",
            "apps/openmw/countsnone.cpp",
            "components/platform/filestdio.cpp",
            "components/toutf8/geniconv.cpp",
            "components/crashcatcher/crashunsupported.cpp",
            "apps/wizard/installationpage.cpp",
            "extern/oics/tinyxml.cpp",
        ]
        # The CS compiled nothing, so its program is off in this flavour; a header is no source; a
        # file of another system, Android's entry, the generator and the other catcher are excused.
        # The system decides which half of a pair is the other's: each build compiles its own.
        # The wizard's unshield half is Windows's to leave out, and every other system's to build. A
        # facility only Linux has is a `linux` file and its absence a `none` one, each the other's.
        forgotten = ["apps/openmw/forgotten.cpp", "components/misc/forgotten.cpp"]
        wizard = "apps/wizard/installationpage.cpp"
        for windows, own, counts in ((False, "components/platform/processposix.cpp", "apps/openmw/countslinux.cpp"),
                                     (True, "components/platform/processwin32.cpp", "apps/openmw/countsnone.cpp")):
            with self.subTest(windows=windows):
                compiled = {"apps/openmw/engine.cpp", "apps/wizard/main.cpp", "components/misc/rng.cpp", own, counts}
                unshield = [] if windows else [wizard]
                self.assertEqual(unlisted(tracked, compiled, windows), sorted([*forgotten, *unshield]))
                self.assertEqual(unlisted(tracked, compiled - {own}, windows), sorted([*forgotten, *unshield, own]))
                self.assertEqual(unlisted(tracked, compiled - {counts}, windows),
                                 sorted([*forgotten, *unshield, counts]))



class QtSourcesTest(unittest.TestCase):
    def test_every_name_of_every_qt_list_is_a_source_of_its_folder(self):
        text = ("add_component_dir (misc\n    strings\n    )\n"
                "if (USE_QT)\n    add_component_qt_dir (config\n        gamesettings\n        launchersettings\n        )\n"
                "    add_component_qt_dir (misc helpviewer scalableicon)\nendif()\n")
        self.assertEqual(qt_sources(text), {"components/config/gamesettings.cpp",
                                            "components/config/launchersettings.cpp",
                                            "components/misc/helpviewer.cpp", "components/misc/scalableicon.cpp"})
        self.assertEqual(qt_sources("add_component_dir (misc strings)\n"), set())

    def test_a_source_a_list_adds_only_with_qt_is_qts(self):
        text = ("target_sources(components-tests PRIVATE always.cpp)\n"
                "if (USE_QT)\n    target_sources(components-tests PRIVATE config/testlaunchersettings.cpp extra.hpp)\n"
                "endif()\n")
        self.assertEqual(qt_guarded_sources(text, "apps/components_tests"),
                         {"apps/components_tests/config/testlaunchersettings.cpp"})
        self.assertEqual(qt_guarded_sources("if(USE_QT)\n    set_property(TARGET a PROPERTY AUTOMOC ON)\nendif(USE_QT)\n",
                                            "apps/launcher"), set())
        # A block nested inside does not end the guarded one at its own `endif`.
        nested = ("if (USE_QT)\n    if (WIN32)\n        target_sources(t PRIVATE win.cpp)\n    endif()\n"
                  "    target_sources(t PRIVATE after.cpp)\nendif()\ntarget_sources(t PRIVATE always.cpp)\n")
        self.assertEqual(qt_guarded_sources(nested, "apps/x"), {"apps/x/win.cpp", "apps/x/after.cpp"})
        # And the block's `else` is the build without Qt, whose sources are no Qt build's.
        branched = ("if (USE_QT)\n    target_sources(t PRIVATE qt.cpp)\nelse()\n"
                    "    target_sources(t PRIVATE plain.cpp)\nendif()\n")
        self.assertEqual(qt_guarded_sources(branched, "apps/x"), {"apps/x/qt.cpp"})

if __name__ == "__main__":
    unittest.main()
