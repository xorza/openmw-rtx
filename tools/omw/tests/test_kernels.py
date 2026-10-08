import struct
import threading
import unittest

from omw.kernels import Setting, SpecConstant, header_count, keyed, map_cancelling, moved, settings, spec_constants
from omw.system import Refusal

MAGIC = 0x07230203


def instruction(opcode: int, *operands: int) -> list[int]:
    return [((1 + len(operands)) << 16) | opcode, *operands]


def string_words(text: str) -> list[int]:
    raw = text.encode() + b"\0"
    raw += b"\0" * (-len(raw) % 4)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def module(order: str = "<", level: str = "LEVEL", signedness: int = 0) -> bytes:
    """%3 a named boolean at SpecId 7, %4 an unnamed one at SpecId 2, %5 a 32-bit integer named `level`
    at SpecId 1, of the signedness given, and %8 a boolean no SpecId decorates."""
    words = [MAGIC, 0x00010600, 0, 20, 0]
    words += instruction(5, 3, *string_words("HAS_MAPS"))
    words += instruction(5, 5, *string_words(level))
    words += instruction(71, 3, 1, 7)
    words += instruction(71, 3, 0)
    words += instruction(71, 4, 1, 2)
    words += instruction(71, 5, 1, 1)
    words += instruction(21, 6, 32, signedness)
    words += instruction(48, 2, 3)
    words += instruction(49, 2, 4)
    words += instruction(50, 6, 5, 16)
    words += instruction(48, 2, 8)
    return struct.pack(f"{order}{len(words)}I", *words)


KNOWN = {"LEVEL": (0, 1, 2)}


class SpecConstantsTest(unittest.TestCase):
    def test_the_constants_a_spec_id_decorates_by_their_id_the_unsigned_over_its_domain(self):
        expected = [SpecConstant(1, "LEVEL", (0, 1, 2)), SpecConstant(2, "constant2", (0, 1), boolean=True),
                    SpecConstant(7, "HAS_MAPS", (0, 1), boolean=True)]
        self.assertEqual(spec_constants(module("<"), KNOWN), expected)
        self.assertEqual(spec_constants(module(">"), KNOWN), expected)

    def test_a_constant_with_no_domain_is_refused_rather_than_left_at_its_default(self):
        for data, says in ((module(level="DEPTH"), "an unsigned specialization constant DEPTH"),
                           (module(signedness=1), "a specialization constant LEVEL")):
            with self.subTest(says=says), self.assertRaises(Refusal) as refused:
                spec_constants(data, KNOWN)
            self.assertIn(says, str(refused.exception))

    def test_a_domain_is_read_from_the_header_that_sizes_it(self):
        self.assertEqual(header_count("    const uint SHADOW_FILTER_LEVELS = 3;\n", "SHADOW_FILTER_LEVELS"), 3)
        with self.assertRaises(Refusal):
            header_count("    const uint SHADOW_FILTER_LEVEL = 3;\n", "SHADOW_FILTER_LEVELS")

    def test_a_module_that_is_not_one_is_refused(self):
        cut = module()[:-4]
        cases = [b"", b"\0" * 21, struct.pack("<5I", 1, 0, 0, 0, 0), cut]
        for data in cases:
            with self.subTest(length=len(data)), self.assertRaises(Refusal):
                spec_constants(data, KNOWN)


class SettingsTest(unittest.TestCase):
    def test_every_setting_the_first_constant_the_lowest_bit(self):
        constants = [SpecConstant(2, "A", (0, 1), boolean=True), SpecConstant(7, "B", (0, 1), boolean=True)]
        self.assertEqual(settings(constants), [
            Setting("2:false 7:false", "A=0,B=0"),
            Setting("2:true 7:false", "A=1,B=0"),
            Setting("2:false 7:true", "A=0,B=1"),
            Setting("2:true 7:true", "A=1,B=1"),
        ])

    def test_an_unsigned_constant_takes_each_value_of_its_domain(self):
        constants = [SpecConstant(0, "SHADOW_LEVEL", (0, 1, 2)), SpecConstant(3, "A", (0, 1), boolean=True)]
        self.assertEqual(settings(constants), [
            Setting("0:0 3:false", "SHADOW_LEVEL=0,A=0"),
            Setting("0:1 3:false", "SHADOW_LEVEL=1,A=0"),
            Setting("0:2 3:false", "SHADOW_LEVEL=2,A=0"),
            Setting("0:0 3:true", "SHADOW_LEVEL=0,A=1"),
            Setting("0:1 3:true", "SHADOW_LEVEL=1,A=1"),
            Setting("0:2 3:true", "SHADOW_LEVEL=2,A=1"),
        ])

    def test_a_module_without_constants_is_one_tuple(self):
        self.assertEqual(settings([]), [Setting("", "")])


class MovedTest(unittest.TestCase):
    def test_a_digest_that_differs_and_a_tuple_one_side_lacks(self):
        before = {"a/-": "1", "b/X=0": "2", "c/-": "3"}
        now = {"a/-": "1", "b/X=0": "9", "d/-": "4"}
        self.assertEqual(moved(before, now), ["b/X=0 2 9", "c/- 3 (none)", "d/- (none) 4"])
        self.assertEqual(moved(before, before), [])

    def test_a_listing_is_keyed_by_module_and_tuple_and_any_other_line_is_refused(self):
        self.assertEqual(keyed(["a - 1", "b X=0,Y=1 2"], "before.txt"), {"a/-": "1", "b/X=0,Y=1": "2"})
        self.assertEqual(keyed([], "before.txt"), {})
        with self.assertRaises(Refusal) as refused:
            keyed(["a - 1", "-- Configuring done (4.1s)"], "before.txt")
        self.assertEqual(str(refused.exception), "before.txt:2 is no `<module> <tuple> <digest>` line of a "
                                                 "listing: '-- Configuring done (4.1s)'")



class MapCancellingTest(unittest.TestCase):
    def test_every_item_in_order_and_the_first_failure_cancels_what_has_not_started(self):
        self.assertEqual(map_cancelling(lambda item: item * 2, [3, 1, 2], 2), [6, 2, 4])
        self.assertEqual(map_cancelling(lambda item: item, [], 2), [])

        # One worker, so the items run one after another: the second refuses, and of the hundred
        # behind it none starts but the one the worker may have taken before the refusal was seen,
        # which stands a tenth of a second as a tuple stands far longer.
        started: list[int] = []
        lock = threading.Lock()

        def refusing(item: int) -> int:
            with lock:
                started.append(item)
            if item == 1:
                raise Refusal("item 1")
            if item > 1:
                threading.Event().wait(0.1)
            return item

        with self.assertRaises(Refusal):
            map_cancelling(refusing, list(range(102)), 1)
        self.assertEqual(started[:2], [0, 1])
        self.assertLessEqual(len(started), 3)

if __name__ == "__main__":
    unittest.main()
