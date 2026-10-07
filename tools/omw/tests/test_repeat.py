import re
import unittest

from omw.build import Build
from omw.repeat import DIFFERED_STATUS, repeat
from omw.system import ROOT, Refusal, read_text


class RepeatTest(unittest.TestCase):
    def test_the_differed_status_is_the_harness_own(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "model" / "benchrun.hpp")
        stated = re.search(r"inline constexpr int sDifferedStatus = (\d+);", text)
        self.assertIsNotNone(stated)
        self.assertEqual(int(stated.group(1)), DIFFERED_STATUS)
        self.assertNotIn(DIFFERED_STATUS, (0, 1), "a run that differed must not read as passed or as failed")

    def test_a_line_that_moves_the_walk_is_refused_before_anything_builds(self):
        walk = "repeat walks `one-cell-walk` for six seconds, always, and {} would move it"
        cases = [
            (["--views=seyda-neen-ship"], walk.format("--views=seyda-neen-ship")),
            (["--suite", "bounce"], walk.format("--suite")),
            (["--seconds=2"], walk.format("--seconds=2")),
            (["--frames=60"], walk.format("--frames=60")),
            (["--pairs=0"], "--pairs=0 is not a count of one or more"),
            (["--upscale=quality"], "repeat sets --upscale itself, on every run"),
            (["--validation=sync"], "repeat sets --validation itself, on every run"),
            (["--hold"], "repeat sets --hold itself, on every run"),
            (["--against=old.csv"], "repeat sets --against itself, on every run"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                with self.assertRaises(Refusal) as refused:
                    repeat(Build("debug"), args)
                self.assertEqual(str(refused.exception), message)


if __name__ == "__main__":
    unittest.main()
