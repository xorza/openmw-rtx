import unittest

from omw.listing import misplaced_checks, unlisted


class UnlistedTest(unittest.TestCase):
    def test_a_source_no_build_compiles_is_named_unless_a_rule_excuses_it(self):
        tracked = [
            "apps/openmw/engine.cpp",
            "apps/openmw/forgotten.cpp",
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
            "apps/wizard/installationpage.cpp",
            "extern/oics/tinyxml.cpp",
        ]
        # A file nothing compiled is forgotten, a header is no source, and the generator and the other system's
        # half of a pair are excused: `linux` against `none`, the wizard's unshield half on Windows.
        forgotten = ["apps/opencs/main.cpp", "apps/openmw/forgotten.cpp", "components/misc/forgotten.cpp"]
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


class MisplacedChecksTest(unittest.TestCase):
    def test_the_checks_reach_the_tree_s_sources_and_no_third_party_s(self):
        root = "/tree"
        database = [
            {"file": "/tree/components/misc/rng.cpp",
             "command": "c++ -Wall -Wzero-as-null-pointer-constant -c rng.cpp"},
            {"file": "/tree/apps/openmw/engine.cpp", "arguments": ["cl", "/W4", "/w14855", "/c", "engine.cpp"]},
            {"file": "/tree/apps/openmw/main.cpp", "command": "c++ -Wall -Wextra -c main.cpp"},
            {"file": "/tree/extern/oics/tinyxml.cpp", "command": "c++ -c tinyxml.cpp"},
            {"file": "/tree/extern/Base64/base64.cpp", "command": "c++ -Wzero-as-null-pointer-constant -c base64.cpp"},
            {"file": "/tree/build-full/_deps/sdl3-src/src/SDL.c", "command": "cc -c SDL.c"},
            {"file": "/tree/build-full/fetched/yaml-cpp/src/node.cpp",
             "command": "c++ -Wzero-as-null-pointer-constant -c node.cpp"},
            {"file": "/tree/build-full/components/osgpluginchecker.cpp",
             "command": "c++ -Wzero-as-null-pointer-constant -c x.cpp"},
            {"file": "/elsewhere/fetched/zlib.c", "command": "cc -Wzero-as-null-pointer-constant -c zlib.c"},
        ]
        # `-Wall -Wextra` is not the checks, which a third party's build adds too: their mark is
        # `-Wzero-as-null-pointer-constant`, or `/w14855`. A source a first-party target generates into the
        # build is neither.
        self.assertEqual(misplaced_checks(database, root), [
            "../elsewhere/fetched/zlib.c: a third party's, compiled with the tree's checks",
            "apps/openmw/main.cpp: compiled without the tree's checks",
            "build-full/fetched/yaml-cpp/src/node.cpp: a third party's, compiled with the tree's checks",
            "extern/Base64/base64.cpp: a third party's, compiled with the tree's checks",
        ])


if __name__ == "__main__":
    unittest.main()
