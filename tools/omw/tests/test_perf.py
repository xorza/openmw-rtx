import unittest

from omw.build import Build
from omw.perf import Measured, measured_window, profile
from omw.system import WINDOWS, Refusal


class MeasuredWindowTest(unittest.TestCase):
    def test_the_window_is_every_places_frames_and_seconds_summed(self):
        record = {"suite": "default", "places": [
            {"view": "a", "frames": 120, "wallSeconds": 2.0},
            {"view": "b", "frames": 30, "wallSeconds": 0.75},
        ]}
        self.assertEqual(measured_window(record), Measured(150, 2.75))
        self.assertEqual(measured_window({"places": []}), Measured(0, 0.0))

    @unittest.skipIf(WINDOWS, "profile is Linux's")
    def test_a_line_that_names_its_own_record_is_refused(self):
        with self.assertRaises(Refusal) as refused:
            profile(Build("release"), ["--json=mine.json"])
        self.assertIn("--json", str(refused.exception))


if __name__ == "__main__":
    unittest.main()
