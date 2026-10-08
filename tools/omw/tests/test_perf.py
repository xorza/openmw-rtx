import contextlib
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path

from omw.build import Build
from omw.perf import Measured, _record_offcpu, measured_window, profile
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



@unittest.skipIf(WINDOWS, "profile is Linux's")
class RecordOffcpuTest(unittest.TestCase):
    """A harness and a recorder that are each a line of Python: the recorder is handed `-p <pid>` as
    perf is, so it can say which harness it was attached to."""

    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.env = dict(os.environ)

    def tearDown(self):
        for path in self.folder.iterdir():
            path.unlink()
        self.folder.rmdir()

    def python(self, code: str, *args: str) -> list[str]:
        return [sys.executable, "-c", code, *args]

    def test_a_recorder_that_refused_is_reported_by_its_own_status(self):
        log = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as told:
            code = _record_offcpu(self.folder, self.env, self.python("import sys; sys.exit(3)"),
                                  self.python("print('frame'); import sys; sys.exit(1)"), log)
        self.assertEqual(code, 3, "the harness's status where perf refused")
        self.assertIn("perf exited with 3", told.getvalue())
        self.assertEqual(log.getvalue(), "frame\n")

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(_record_offcpu(self.folder, self.env, self.python("pass"),
                                            self.python("import sys; sys.exit(4)"), log), 4)

    def test_a_reader_that_stops_takes_the_harness_down(self):
        class Closed(io.StringIO):
            def write(self, text: str) -> int:
                raise BrokenPipeError

        attached = self.folder / "attached"
        recorder = self.python("import sys; open(sys.argv[1], 'w').write(sys.argv[3])", str(attached))
        harness = self.python("import time; print('frame', flush=True); time.sleep(30)")
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(BrokenPipeError):
            _record_offcpu(self.folder, self.env, recorder, harness, Closed())
        with self.assertRaises(ProcessLookupError, msg="the harness outlived its reader"):
            os.kill(int(attached.read_text()), 0)

if __name__ == "__main__":
    unittest.main()
