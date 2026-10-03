import unittest

from omw.spellings import narrowed


class NarrowedTest(unittest.TestCase):
    def test_a_path_narrowed_in_code_is_named_and_one_in_a_comment_or_spelled_whole_is_not(self):
        text = (
            "const std::string a = path.string();\n"
            "// path.string() says what the old spelling did\n"
            "const std::string b = Files::pathToUnicodeString(path);\n"
            "const std::u8string c = path.u8string();\n"
            "const std::string d = path.generic_string(); // narrowed\n"
        )
        self.assertEqual([line.split(":")[1] for line in narrowed("a.cpp", text)], ["1", "5"])


if __name__ == "__main__":
    unittest.main()
