import struct
import unittest

from omw.kernels import Setting, SpecBool, moved, settings, spec_bools
from omw.system import Refusal

MAGIC = 0x07230203


def instruction(opcode: int, *operands: int) -> list[int]:
    return [((1 + len(operands)) << 16) | opcode, *operands]


def string_words(text: str) -> list[int]:
    raw = text.encode() + b"\0"
    raw += b"\0" * (-len(raw) % 4)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def module(order: str = "<") -> bytes:
    """%3 a named boolean at SpecId 7, %4 an unnamed one at SpecId 2, %5 a uint at SpecId 1, and %8 a
    boolean no SpecId decorates."""
    words = [MAGIC, 0x00010600, 0, 20, 0]
    words += instruction(5, 3, *string_words("HAS_MAPS"))
    words += instruction(71, 3, 1, 7)
    words += instruction(71, 3, 0)
    words += instruction(71, 4, 1, 2)
    words += instruction(71, 5, 1, 1)
    words += instruction(48, 2, 3)
    words += instruction(49, 2, 4)
    words += instruction(50, 6, 5, 16)
    words += instruction(48, 2, 8)
    return struct.pack(f"{order}{len(words)}I", *words)


class SpecBoolsTest(unittest.TestCase):
    def test_the_booleans_a_spec_id_decorates_by_their_id(self):
        expected = [SpecBool(2, "constant2"), SpecBool(7, "HAS_MAPS")]
        self.assertEqual(spec_bools(module("<")), expected)
        self.assertEqual(spec_bools(module(">")), expected)

    def test_a_module_that_is_not_one_is_refused(self):
        cut = module()[:-4]
        cases = [b"", b"\0" * 21, struct.pack("<5I", 1, 0, 0, 0, 0), cut]
        for data in cases:
            with self.subTest(length=len(data)), self.assertRaises(Refusal):
                spec_bools(data)


class SettingsTest(unittest.TestCase):
    def test_every_setting_the_first_constant_the_lowest_bit(self):
        constants = [SpecBool(2, "A"), SpecBool(7, "B")]
        self.assertEqual(settings(constants), [
            Setting("2:false 7:false", "A=0,B=0"),
            Setting("2:true 7:false", "A=1,B=0"),
            Setting("2:false 7:true", "A=0,B=1"),
            Setting("2:true 7:true", "A=1,B=1"),
        ])

    def test_a_module_without_constants_is_one_tuple(self):
        self.assertEqual(settings([]), [Setting("", "")])


class MovedTest(unittest.TestCase):
    def test_a_digest_that_differs_and_a_tuple_one_side_lacks(self):
        before = {"a/-": "1", "b/X=0": "2", "c/-": "3"}
        now = {"a/-": "1", "b/X=0": "9", "d/-": "4"}
        self.assertEqual(moved(before, now), ["b/X=0 2 9", "c/- 3 (none)", "d/- (none) 4"])
        self.assertEqual(moved(before, before), [])


if __name__ == "__main__":
    unittest.main()
