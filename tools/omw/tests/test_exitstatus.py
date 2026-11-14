import re
import unittest

from omw.exitstatus import ExitStatus
from omw.system import ROOT, read_text


class ExitStatusTest(unittest.TestCase):
    def test_every_status_is_the_harness_own(self):
        text = read_text(ROOT / "apps" / "rtxtool" / "model" / "exitstatus.hpp")
        body = re.search(r"enum class ExitStatus : int\s*\{(.*?)\};", text, re.S)
        self.assertIsNotNone(body)
        stated = {name.upper(): int(value) for name, value in re.findall(r"^\s*(\w+) = (\d+),", body.group(1), re.M)}
        self.assertEqual(stated, {status.name: status.value for status in ExitStatus})
