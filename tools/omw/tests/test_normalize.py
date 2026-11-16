import shutil
import tempfile
import unittest
from pathlib import Path, PurePosixPath

from omw.normalize import Exists, Unnormalizable, normalized, spans

# A source of the tree, so the rules reach it.
NAME = "apps/openmw/mwworld/thing.cpp"


def integers(text: str, name: str = NAME) -> str:
    return normalized(name, text, rules=["integers"])


class SpansTest(unittest.TestCase):
    def test_the_pieces_join_to_the_text_and_only_code_is_code(self):
        text = ('int a = 1\'000; // int b\nauto s = "int"; auto r = R"x(int "q")x"; /* int\nc */ char c = \'i\';\n'
                'auto w = u8"int"; auto l = L\'x\';\n')
        pieces = spans(text)
        self.assertEqual("".join(piece.text for piece in pieces), text)
        code = "".join(piece.text for piece in pieces if piece.code)
        self.assertEqual(code, "int a = 1'000; \nauto s = ; auto r = ;  char c = ;\nauto w = ; auto l = ;\n")


class IntegersTest(unittest.TestCase):
    def test_every_spelling_becomes_its_fixed_width_and_the_rest_stand(self):
        cases = [
            ("int a;", "std::int32_t a;"),
            ("signed a;", "std::int32_t a;"),
            ("signed int a;", "std::int32_t a;"),
            ("unsigned a;", "std::uint32_t a;"),
            ("unsigned int a;", "std::uint32_t a;"),
            ("short a;", "std::int16_t a;"),
            ("short int a;", "std::int16_t a;"),
            ("signed short a;", "std::int16_t a;"),
            ("unsigned short a;", "std::uint16_t a;"),
            ("unsigned short int a;", "std::uint16_t a;"),
            ("unsigned char a;", "std::uint8_t a;"),
            ("signed char a;", "std::int8_t a;"),
            ("uint32_t a;", "std::uint32_t a;"),
            ("int_fast8_t a;", "std::int_fast8_t a;"),
            ("uintptr_t a;", "std::uintptr_t a;"),
            ("size_t a;", "std::size_t a;"),
            ("ptrdiff_t a;", "std::ptrdiff_t a;"),
            ("static_cast<int>(x)", "static_cast<std::int32_t>(x)"),
            ("std::make_unsigned_t<int> z;", "std::make_unsigned_t<std::int32_t> z;"),
            ("operator unsigned int() const;", "operator std::uint32_t() const;"),
            # Left as it stands: text, a width the system chooses, a name already qualified, a member.
            ("char a;", "char a;"),
            ("long a;", "long a;"),
            ("unsigned long a;", "unsigned long a;"),
            ("long long int a;", "long long int a;"),
            ("std::size_t a;", "std::size_t a;"),
            ("::size_t a;", "::size_t a;"),
            ("v->size_t; v.size_t;", "v->size_t; v.size_t;"),
            ("char16_t a; intern b; uint c; ssize_t d;", "char16_t a; intern b; uint c; ssize_t d;"),
            ("int main(int argc, char** argv)", "int main(std::int32_t argc, char** argv)"),
        ]
        for before, after in cases:
            with self.subTest(before=before):
                body = integers(f"#include <vector>\n\n{before}\n").split("\n\n", 1)[1]
                self.assertEqual(body, f"{after}\n")

    def test_strings_comments_raw_strings_and_wordless_directives_are_untouched(self):
        text = ("#include <vector>\n#include <a/int.hpp>\n#error int is not long\n#pragma warning(disable : 4)\n"
                '// unsigned int\nconst char* s = "short int"; auto r = R"(int)"; /* int */\n')
        self.assertEqual(integers(text), text)

    def test_cstdint_and_cstddef_join_the_standard_headers_only_where_a_new_name_needs_them(self):
        self.assertEqual(integers('#include "thing.hpp"\n\n#include <vector>\n\nint a;\nsize_t b;\n'),
                         '#include "thing.hpp"\n\n#include <cstddef>\n#include <cstdint>\n#include <vector>\n\n'
                         "std::int32_t a;\nstd::size_t b;\n")
        self.assertEqual(integers("#include <cstdint>\n#include <vector>\n\nint a;\n"),
                         "#include <cstdint>\n#include <vector>\n\nstd::int32_t a;\n")
        # No standard header: a block of its own after the file's own header, or after `#pragma once`.
        self.assertEqual(integers('#include "thing.hpp"\n#include <osg/Node>\n\nint a;\n'),
                         '#include "thing.hpp"\n\n#include <cstdint>\n#include <osg/Node>\n\nstd::int32_t a;\n')
        self.assertEqual(integers("#pragma once\n\nint a;\n", "apps/openmw/mwworld/thing.hpp"),
                         "#pragma once\n\n#include <cstdint>\n\nstd::int32_t a;\n")

    def test_a_line_the_lint_exception_marks_keeps_its_keyword(self):
        text = ("#include <cstdint>\n\n"
                "static_assert(std::is_same_v<std::int32_t, int>); // NOLINT(google-runtime-int): why\n"
                "// NOLINTNEXTLINE(google-runtime-int): why\nshort s;\nint i;\n")
        self.assertEqual(integers(text), text.replace("int i;", "std::int32_t i;"))

    def test_a_type_over_two_lines_is_one_and_a_comment_inside_one_refuses_the_file(self):
        self.assertEqual(integers("#include <vector>\n\nunsigned\n    int a;\n"),
                         "#include <cstdint>\n#include <vector>\n\nstd::uint32_t a;\n")
        with self.assertRaises(Unnormalizable):
            integers("#include <vector>\n\nunsigned /* wide */ int a;\n")

    def test_a_new_header_joins_the_standard_ones_and_not_a_librarys(self):
        self.assertEqual(integers("#include <QDialog>\n#include <vector>\n\nint a;\n"),
                         "#include <QDialog>\n#include <cstdint>\n#include <vector>\n\nstd::int32_t a;\n")

    def test_a_type_inside_a_stringifying_macro_refuses_the_file(self):
        with self.assertRaises(Unnormalizable):
            integers('#include <QObject>\n\nQMetaObject::invokeMethod(this, "f", Q_ARG(int, 1));\n')
        text = '#include <QObject>\n\nconnect(a, SIGNAL(clicked()), b, SLOT(close()));\nint n;\n'
        self.assertIn("std::int32_t n;", integers(text))

    def test_it_is_idempotent_and_reaches_only_the_trees_cpp(self):
        text = "#include <vector>\n\nunsigned short a;\nint b;\nsize_t c;\n"
        once = integers(text)
        self.assertEqual(integers(once), once)
        for name in ("extern/sol/x.hpp", "components/rtx/shaders/scene.h", "tools/omw/x.cpp", "apps/x/a.py"):
            with self.subTest(name=name):
                self.assertEqual(normalized(name, text, rules=["integers"]), text)


class IncludesTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        for name in ("apps/openmw/mwbase/world.hpp", "apps/openmw/mwworld/ptr.hpp", "apps/openmw/mwworld/sub/a.hpp"):
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text("")
        self.exists = Exists(self.root)

    def includes(self, text: str) -> str:
        return normalized(NAME, text, rules=["includes"], exists=self.exists)

    def test_another_folders_header_is_spelled_from_the_root_and_the_files_own_folder_stays_quoted(self):
        self.assertEqual(
            self.includes('#include "../mwbase/world.hpp"\n#include "./ptr.hpp"\n#include "sub/a.hpp"\n'
                          '#include "ptr.hpp"\n#include "ui_dialog.h"\n#include "../nowhere/x.hpp"\n'
                          "#include <osg/Node>\n"),
            '#include <apps/openmw/mwbase/world.hpp>\n#include "ptr.hpp"\n#include <apps/openmw/mwworld/sub/a.hpp>\n'
            '#include "ptr.hpp"\n#include "ui_dialog.h"\n#include "../nowhere/x.hpp"\n#include <osg/Node>\n')

    def test_a_path_from_the_root_is_angled_and_one_of_the_files_own_folder_quoted(self):
        self.assertEqual(
            self.includes('#include "apps/openmw/mwbase/world.hpp"\n#include <apps/openmw/mwworld/ptr.hpp>\n'
                          "#include <apps/openmw/mwbase/world.hpp>\n#include <components/missing.hpp>\n"),
            '#include <apps/openmw/mwbase/world.hpp>\n#include "ptr.hpp"\n#include <apps/openmw/mwbase/world.hpp>\n'
            "#include <components/missing.hpp>\n")

    def test_a_path_that_climbs_out_of_the_tree_stands(self):
        self.assertFalse(self.exists(PurePosixPath("apps/missing.hpp")))
        text = '#include "../../../../outside.hpp"\n'
        self.assertEqual(self.includes(text), text)


if __name__ == "__main__":
    unittest.main()
