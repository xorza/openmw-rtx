import unittest
from pathlib import Path

from omw.testing import ctest_arguments, durations

ROOT = Path("/checkout")
FORK = frozenset({"apps/components_tests/rtx/environment/fogbuilder.cpp",
                  "apps/components_tests/rtxvulkan/trace/visibility/specular.cpp",
                  "apps/components_tests/widgets/listbox.cpp"})


def case(name: str, time: str, file: str) -> dict:
    return {"name": name, "time": time, "file": str(ROOT / file)}


class DurationsTest(unittest.TestCase):
    def test_the_forks_cases_by_their_full_names_in_seconds_beside_upstreams_files_too(self):
        report = {"testsuites": [
            {"name": "RtxFogNoiseTest", "testsuite": [
                case("theBand", "0.012s", "apps/components_tests/rtx/environment/fogbuilder.cpp"),
                case("everyLevel", "1.333s", "apps/components_tests/rtx/environment/fogbuilder.cpp"),
            ]},
            {"name": "Prefix/Typed", "testsuite": [
                case("case/3", "0s", "apps/components_tests/rtxvulkan/trace/visibility/specular.cpp"),
            ]},
            {"name": "ListBoxTest", "testsuite": [
                case("aList", "0.04s", "apps/components_tests/widgets/listbox.cpp"),
            ]},
        ]}
        self.assertEqual(durations(report, FORK, ROOT), {
            "RtxFogNoiseTest.theBand": 0.012,
            "RtxFogNoiseTest.everyLevel": 1.333,
            "Prefix/Typed.case/3": 0.0,
            "ListBoxTest.aList": 0.04,
        })

    def test_upstreams_cases_and_cases_outside_the_checkout_are_left_out(self):
        report = {"testsuites": [{"name": "DetourNavigatorNavigatorTest", "testsuite": [
            case("update_should_be_limited", "1.28s", "apps/components_tests/detournavigator/navigator.cpp"),
            {"name": "elsewhere", "time": "2s", "file": "/usr/include/gtest/gtest.h"},
        ]}]}
        self.assertEqual(durations(report, FORK, ROOT), {})

    def test_a_report_without_suites_has_no_cases(self):
        self.assertEqual(durations({"tests": 0}, FORK, ROOT), {})



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
