#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxbuild/run.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/digestwords.hpp>
#include <components/rtx/renderer/shaderset.hpp>
#include <components/testing/util.hpp>

namespace RtxBuild
{
    namespace
    {
        struct Ran
        {
            int mStatus;
            std::string mErrors;
        };

        /// `words` past the program's name, as a command line hands them.
        Ran runLine(std::vector<std::string> words)
        {
            words.insert(words.begin(), "openmw-rtx-build");
            std::vector<char*> line;
            for (std::string& word : words)
                line.push_back(word.data());
            std::ostringstream errors;
            const int status = run(line, errors);
            return Ran{ status, errors.str() };
        }

        std::string readText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }

        /// **A line no verb takes is refused with two and says why**: no verb lists every verb, a verb that is not
        /// one is named, and a verb given the wrong count of arguments says what it takes, its fewest and its most
        /// both held.
        TEST(RtxBuildRunTest, aLineNoVerbTakesIsRefusedWithTwoAndSaysWhy)
        {
            const Ran none = runLine({});
            EXPECT_EQ(none.mStatus, 2);
            EXPECT_TRUE(none.mErrors.starts_with("usage: openmw-rtx-build <verb> <arguments>")) << none.mErrors;
            EXPECT_NE(none.mErrors.find("\n  pin <module.spv>\n"), std::string::npos) << none.mErrors;
            EXPECT_NE(none.mErrors.find("\n  specular-albedo <table.cpp>\n"), std::string::npos) << none.mErrors;

            const Ran unknown = runLine({ "pine", "a.spv" });
            EXPECT_EQ(unknown.mStatus, 2);
            EXPECT_TRUE(unknown.mErrors.starts_with("openmw-rtx-build: no verb is called pine\nusage:"))
                << unknown.mErrors;

            for (const std::vector<std::string>& wrong : std::vector<std::vector<std::string>>{
                     { "pin" }, { "pin", "a.spv", "b.spv" }, { "embed", "out.cpp" }, { "digest", "a.spv" } })
            {
                const Ran ran = runLine(wrong);
                EXPECT_EQ(ran.mStatus, 2) << wrong.front();
                EXPECT_TRUE(ran.mErrors.starts_with("openmw-rtx-build " + wrong.front() + ": takes ")) << ran.mErrors;
            }
            EXPECT_EQ(runLine({ "pin" }).mErrors, "openmw-rtx-build pin: takes <module.spv>\n");
        }

        /// A verb that fails exits with one and its error after its name; one that runs, with nought and nothing said.
        TEST(RtxBuildRunTest, aVerbThatFailsSaysSoUnderItsNameAndOneThatRunsSaysNothing)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            std::filesystem::create_directories(root);

            const Ran missing = runLine({ "pin", Files::pathToUnicodeString(root / "missing.spv") });
            EXPECT_EQ(missing.mStatus, 1);
            EXPECT_TRUE(missing.mErrors.starts_with("openmw-rtx-build pin: cannot read ")) << missing.mErrors;

            const std::filesystem::path table = root / "fognoise.cpp";
            const Ran made = runLine({ "fog-noise", Files::pathToUnicodeString(table) });
            EXPECT_EQ(made.mStatus, 0);
            EXPECT_EQ(made.mErrors, "");
            EXPECT_TRUE(readText(table).starts_with("// Made by the build")) << table;
        }

        /// **`embed` writes each module under its file's name, in name order, with the digest a set of the same
        /// modules has**, so the compiled-in set and one read from a folder key the driver's caches alike.
        TEST(RtxBuildRunTest, embedWritesTheModulesInNameOrderWithTheirSetsDigest)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            std::filesystem::create_directories(root);
            constexpr std::array<std::uint32_t, 2> first{ 0x07230203, 1 };
            constexpr std::array<std::uint32_t, 2> second{ 0x07230203, 2 };
            for (const auto& [name, words] : { std::pair{ "b.spv", second }, std::pair{ "a.spv", first } })
            {
                std::ofstream stream(root / name, std::ios::binary);
                const std::span<const std::byte> bytes = std::as_bytes(std::span(words));
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }

            const std::filesystem::path source = root / "shipped.cpp";
            const Ran ran = runLine({ "embed", Files::pathToUnicodeString(source),
                Files::pathToUnicodeString(root / "b.spv"), Files::pathToUnicodeString(root / "a.spv") });
            ASSERT_EQ(ran.mStatus, 0) << ran.mErrors;

            const std::string text = readText(source);
            EXPECT_NE(text.find("std::array<std::uint32_t, 2> sModule0{ {\n            0x07230203, 0x00000001,\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("{ \"a.spv\", sModule0 },\n            { \"b.spv\", sModule1 },"), std::string::npos)
                << text;

            const std::array<Rtx::ShaderModule, 2> modules{ { { "a.spv", first }, { "b.spv", second } } };
            const Misc::DigestWords digest = Rtx::digestShaders(modules);
            EXPECT_NE(
                text.find(std::format("{{ {{ 0x{:016x}u, 0x{:016x}u }} }}", digest[0], digest[1])), std::string::npos)
                << text;
        }
    }
}
