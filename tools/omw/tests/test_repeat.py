import re
import unittest

from omw.repeat import DIFFERED_STATUS
from omw.system import ROOT


class RepeatTest(unittest.TestCase):
    def test_the_differed_status_is_the_harness_own(self):
        text = (ROOT / "apps" / "rtxtool" / "model" / "benchrun.hpp").read_text()
        stated = re.search(r"inline constexpr int sDifferedStatus = (\d+);", text)
        self.assertIsNotNone(stated)
        self.assertEqual(int(stated.group(1)), DIFFERED_STATUS)
        self.assertNotIn(DIFFERED_STATUS, (0, 1), "a run that differed must not read as passed or as failed")


if __name__ == "__main__":
    unittest.main()
