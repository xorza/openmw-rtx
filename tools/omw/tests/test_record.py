import json
import unittest

from omw.record import Record, read_record


class ReadRecordTest(unittest.TestCase):
    def test_a_record_is_its_header_its_places_in_order_and_the_last_findings(self):
        text = ('{"header": {"suite": "tour", "frames": 240}}\n'
                '{"place": {"view": "leg-1", "frames": 240}}\n'
                '{"findings": {"faults": 0, "costs": 1, "expected": 0}}\n'
                '\n'
                '{"place": {"view": "leg-2", "frames": 200}}\n'
                '{"findings": {"faults": 1, "costs": 1, "expected": 0}}\n')
        self.assertEqual(read_record(text), Record(
            {"suite": "tour", "frames": 240},
            ({"view": "leg-1", "frames": 240}, {"view": "leg-2", "frames": 200}),
            {"faults": 1, "costs": 1, "expected": 0}))

    def test_a_bench_states_no_findings_and_an_empty_text_is_an_empty_record(self):
        self.assertEqual(read_record('{"header": {}}\n{"place": {"view": "a"}}\n'),
                         Record({}, ({"view": "a"},), None))
        self.assertEqual(read_record(""), Record({}, (), None))

    def test_a_last_line_cut_short_is_left_out_and_one_cut_short_before_it_is_no_record(self):
        died = '{"header": {}}\n{"place": {"view": "leg-1"}}\n{"place": {"view": "le'
        self.assertEqual(read_record(died), Record({}, ({"view": "leg-1"},), None))
        with self.assertRaises(json.JSONDecodeError):
            read_record('{"header": {}}\n{"place": {"vi\n{"place": {"view": "leg-2"}}\n')
