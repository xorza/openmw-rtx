import unittest
from pathlib import Path

from omw.testing import RUNS_ALONE, ctest_arguments, durations, least_alone

ROOT = Path("/checkout")
FORK = frozenset({"apps/components_tests/rtx/environment/fogbuilder.cpp",
                  "apps/components_tests/rtxvulkan/trace/visibility/specular.cpp",
                  "apps/components_tests/widgets/listbox.cpp"})


def case(name: str, time: str, file: str) -> dict:
    return {"name": name, "time": time, "file": str(ROOT / file)}


class LeastAloneTest(unittest.TestCase):
    def runs(self, seconds: list[float]) -> tuple[float, int]:
        taken = iter(seconds)
        asked = 0

        def run() -> float:
            nonlocal asked
            asked += 1
            return next(taken)

        least = least_alone(run, limit=2.0)
        return least, asked

    def test_the_least_of_up_to_three_runs_and_no_more_once_one_is_inside_the_limit(self):
        self.assertEqual(RUNS_ALONE, 3)
        # Inside at once: one run.
        self.assertEqual(self.runs([1.4]), (1.4, 1))
        # On the limit passes, as `timing` compares with `>`: one run.
        self.assertEqual(self.runs([2.0]), (2.0, 1))
        # A burst, then a quiet run: two runs, the quiet one counted.
        self.assertEqual(self.runs([2.3, 1.7]), (1.7, 2))
        # Over every time: three runs, the least of them, and never a fourth.
        self.assertEqual(self.runs([2.3, 2.2, 2.4, 1.0]), (2.2, 3))


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
