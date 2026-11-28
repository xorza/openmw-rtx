import unittest
from collections.abc import Iterator
from pathlib import Path

from omw.testing import RUNS, ctest_arguments, durations, least_of_runs, suite_seconds

ROOT = Path("/checkout")
FORK = frozenset({"apps/components_tests/rtx/environment/fogbuilder.cpp",
                  "apps/components_tests/rtxvulkan/trace/visibility/specular.cpp",
                  "apps/components_tests/widgets/listbox.cpp"})


def case(name: str, time: str, file: str) -> dict:
    return {"name": name, "time": time, "file": str(ROOT / file)}


class LeastOfRunsTest(unittest.TestCase):
    def runs(self, seconds: list[dict[str, float]]) -> tuple[dict[str, float], int]:
        asked = 0

        def run() -> Iterator[dict[str, float]]:
            nonlocal asked
            for one in seconds:
                asked += 1
                yield one

        least = least_of_runs(run(), limit=2.0)
        return least, asked

    def test_the_least_of_up_to_three_runs_and_no_more_once_one_is_inside_the_limit(self):
        self.assertEqual(RUNS, 3)
        # Inside at once: one run.
        self.assertEqual(self.runs([{"a": 1.4}]), ({"a": 1.4}, 1))
        # On the limit passes, as `timing` compares with `>`: one run.
        self.assertEqual(self.runs([{"a": 2.0}]), ({"a": 2.0}, 1))
        # A burst, then a quiet run: two runs, the quiet one counted.
        self.assertEqual(self.runs([{"a": 2.3}, {"a": 1.7}]), ({"a": 1.7}, 2))
        # Over every time: three runs, the least of them, and never a fourth.
        self.assertEqual(self.runs([{"a": 2.3}, {"a": 2.2}, {"a": 2.4}, {"a": 1.0}]), ({"a": 2.2}, 3))

    def test_each_name_keeps_its_own_least_and_one_over_runs_them_all_again(self):
        # `b` alone is over at first, so all run again; `a`, inside already, keeps its least, 1.0 of 1.0 and 1.5,
        # and `b` comes inside on the second run, 1.9 against 4.0: no third.
        self.assertEqual(self.runs([{"a": 1.0, "b": 4.0}, {"a": 1.5, "b": 1.9}, {"a": 0.1, "b": 0.1}]),
                         ({"a": 1.0, "b": 1.9}, 2))
        # The least of each over three, from different runs: `a` 1.2 from the second, `b` 2.5 from the third.
        self.assertEqual(self.runs([{"a": 3.0, "b": 2.6}, {"a": 1.2, "b": 2.9}, {"a": 2.1, "b": 2.5}]),
                         ({"a": 1.2, "b": 2.5}, 3))


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

    def test_a_suites_seconds_are_its_reports_whole_time_and_nought_where_it_states_none(self):
        self.assertEqual(suite_seconds({"time": "31.25s", "testsuites": []}), 31.25)
        self.assertEqual(suite_seconds({"time": "0.004s"}), 0.004)
        self.assertEqual(suite_seconds({}), 0.0)

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
