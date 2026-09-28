#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/error.hpp>
#include <components/rtxvulkan/spirvfile.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        void writeBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        /// The message `readSpirv` refuses `path` with, and nothing where it reads it.
        std::string refusalOf(const std::filesystem::path& path)
        {
            try
            {
                readSpirv(path);
            }
            catch (const InputError& refused)
            {
                return refused.what();
            }
            return {};
        }

        /// **The one reader takes a module as its words and refuses anything else, naming the file**
        /// — for the renderer's modules and for the build's pin tool alike, which read no magic
        /// number while each had a reader of its own.
        ///
        /// By hand: the magic 0x07230203 then 0x00010600 is two words, little-endian on the file;
        /// six bytes is a word and a half; two whole words of anything else begin with no magic.
        TEST(RtxSpirvFileTest, aModuleIsItsWordsAndAnythingElseIsRefusedByName)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();

            constexpr std::array<std::uint8_t, 8> module{ 0x03, 0x02, 0x23, 0x07, 0x00, 0x06, 0x01, 0x00 };
            writeBytes(root / "module.spv", module);
            EXPECT_EQ(readSpirv(root / "module.spv"), (std::vector<std::uint32_t>{ 0x07230203, 0x00010600 }));

            constexpr std::array<std::uint8_t, 6> ragged{ 0x03, 0x02, 0x23, 0x07, 0x00, 0x06 };
            writeBytes(root / "ragged.spv", ragged);
            const std::string raggedRefusal = refusalOf(root / "ragged.spv");
            EXPECT_NE(raggedRefusal.find("ragged.spv is 6 bytes"), std::string::npos) << raggedRefusal;

            constexpr std::array<std::uint8_t, 8> text{ 'n', 'o', 't', ' ', 's', 'p', 'i', 'r' };
            writeBytes(root / "text.spv", text);
            const std::string textRefusal = refusalOf(root / "text.spv");
            EXPECT_NE(textRefusal.find("text.spv does not begin with the SPIR-V magic number"), std::string::npos)
                << textRefusal;

            const std::string missingRefusal = refusalOf(root / "missing.spv");
            EXPECT_NE(missingRefusal.find("cannot open"), std::string::npos) << missingRefusal;
        }
    }
}
