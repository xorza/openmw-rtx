#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include <components/rtx/common/tablesource.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        std::string textOf(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        /// **Each float in hexadecimal, its sign apart, twelve to a line.** A half is `1p-1`, a third
        /// rounds to the float `0x3eaaaaab`, whose 23 bits shifted to six digits are `555556`, and a
        /// tenth is `0x3dcccccd`, `99999a`; the largest float is all ones. A negative zero keeps its
        /// sign, and the thirteenth value opens a line of its own.
        TEST(RtxTableSourceTest, aFloatTableIsItsValuesToTheBit)
        {
            const std::filesystem::path to = TestingOpenMW::currentTestDirPath() / "floats.cpp";
            const std::array<float, 13> values{ 0.5f, -0.25f, 0.0f, -0.0f, 1.0f / 3.0f, 1.0f, 3.0f, 0.1f,
                std::numeric_limits<float>::max(), 2.0f, 0.75f, 0.125f, 1.5f };
            writeTable(to, TableName{ .mHeader = "components/rtx/frame/floats.hpp", .mName = "sFloats" }, values);

            EXPECT_EQ(textOf(to),
                "// Made by the build, by the maker `sFloats`'s header names. Never edited.\n"
                "\n"
                "#include <array>\n"
                "#include <cstdint>\n"
                "\n"
                "#include <components/rtx/frame/floats.hpp>\n"
                "\n"
                "namespace Rtx\n"
                "{\n"
                "    constinit const std::array<float, 13> sFloats{ {\n"
                "        0x1p-1f, -0x1p-2f, 0x0p+0f, -0x0p+0f, 0x1.555556p-2f, 0x1p+0f, 0x1.8p+1f, 0x1.99999ap-4f, "
                "0x1.fffffep+127f, 0x1p+1f, 0x1.8p-1f, 0x1p-3f,\n"
                "        0x1.8p+0f,\n"
                "    } };\n"
                "}\n");
            EXPECT_FALSE(std::filesystem::exists(TestingOpenMW::currentTestDirPath() / "floats.cpp.part"));
        }

        /// A byte in decimal, and a table written again over one that stands replaces it whole.
        TEST(RtxTableSourceTest, aByteTableIsItsValuesAndReplacesTheOneBefore)
        {
            const std::filesystem::path to = TestingOpenMW::currentTestDirPath() / "bytes.cpp";
            const TableName name{ .mHeader = "components/rtx/world/bytes.hpp", .mName = "sBytes" };
            writeTable(to, name, std::array<std::uint8_t, 4>{ 1, 2, 3, 4 });
            writeTable(to, name, std::array<std::uint8_t, 3>{ 0, 7, 255 });

            const std::string text = textOf(to);
            EXPECT_NE(
                text.find("    constinit const std::array<std::uint8_t, 3> sBytes{ {\n        0, 7, 255,\n    } };\n"),
                std::string::npos)
                << text;
            EXPECT_EQ(text.find("4> sBytes"), std::string::npos) << text;
        }

        /// A table that cannot be written is the filesystem's error, naming the file it could not
        /// write, and leaves no table behind.
        TEST(RtxTableSourceTest, aTableThatCannotBeWrittenIsAnErrorNamingItsFile)
        {
            const std::filesystem::path to = TestingOpenMW::currentTestDirPath() / "missing" / "table.cpp";
            try
            {
                writeTable(to, TableName{ .mHeader = "components/rtx/world/bytes.hpp", .mName = "sBytes" },
                    std::array<std::uint8_t, 1>{ 0 });
                ADD_FAILURE() << "a table written into a folder that is not there";
            }
            catch (const std::filesystem::filesystem_error& error)
            {
                std::filesystem::path part = to;
                part += ".part";
                EXPECT_EQ(error.path1(), part);
            }
            EXPECT_FALSE(std::filesystem::exists(to));
        }
    }
}
