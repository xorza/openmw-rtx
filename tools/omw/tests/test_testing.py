import unittest
from pathlib import Path

from omw.testing import durations

ROOT = Path("/checkout")


def case(name: str, time: str, file: str) -> dict:
    return {"name": name, "time": time, "file": str(ROOT / file)}


class DurationsTest(unittest.TestCase):
    def test_the_forks_cases_by_their_full_names_in_seconds(self):
        report = {"testsuites": [
            {"name": "RtxFogNoiseTest", "testsuite": [
                case("theBand", "0.012s", "apps/components_tests/rtx/environment/fogbuilder.cpp"),
                case("everyLevel", "1.333s", "apps/components_tests/rtx/environment/fogbuilder.cpp"),
            ]},
            {"name": "Prefix/Typed", "testsuite": [
                case("case/3", "0s", "apps/components_tests/rtxvulkan/trace/visibility/specular.cpp"),
            ]},
        ]}
        self.assertEqual(durations(report, ROOT), {
            "RtxFogNoiseTest.theBand": 0.012,
            "RtxFogNoiseTest.everyLevel": 1.333,
            "Prefix/Typed.case/3": 0.0,
        })

    def test_upstreams_cases_and_cases_outside_the_checkout_are_left_out(self):
        report = {"testsuites": [{"name": "DetourNavigatorNavigatorTest", "testsuite": [
            case("update_should_be_limited", "1.28s", "apps/components_tests/detournavigator/navigator.cpp"),
            {"name": "elsewhere", "time": "2s", "file": "/usr/include/gtest/gtest.h"},
        ]}]}
        self.assertEqual(durations(report, ROOT), {})

    def test_a_report_without_suites_has_no_cases(self):
        self.assertEqual(durations({"tests": 0}, ROOT), {})


if __name__ == "__main__":
    unittest.main()
