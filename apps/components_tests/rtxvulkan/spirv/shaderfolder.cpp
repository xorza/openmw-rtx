#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/shaderset.hpp>
#include <components/rtxvulkan/createrenderer.hpp>
#include <components/rtxvulkan/spirv/shaderfolder.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sMagic = 0x07230203;

        void writeModule(const std::filesystem::path& file, const std::vector<std::uint32_t>& words)
        {
            std::filesystem::create_directories(file.parent_path());
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            const std::span<const std::byte> bytes = std::as_bytes(std::span(words));
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        /// **A folder is read whole, by name**: every `.spv` in name order and nothing else, each module's words
        /// as the file holds them, and the digest the same modules in memory have.
        TEST(RtxShaderFolderTest, everyModuleIsReadInNameOrderWithTheDigestOfItsModules)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "shaders";
            writeModule(folder / "b.comp.spv", { sMagic, 2 });
            writeModule(folder / "a.comp.spv", { sMagic, 1, 1 });
            writeModule(folder / "notes.txt", { 9 });

            const ShaderFolder read(folder, true);
            const ShaderSet& set = read.getSet();
            ASSERT_EQ(set.mModules.size(), 2u);
            EXPECT_EQ(set.mModules[0].mName, "a.comp.spv");
            EXPECT_EQ(set.mModules[1].mName, "b.comp.spv");
            EXPECT_EQ(std::vector(set.mModules[0].mWords.begin(), set.mModules[0].mWords.end()),
                (std::vector<std::uint32_t>{ sMagic, 1, 1 }));
            EXPECT_EQ(std::vector(set.mModules[1].mWords.begin(), set.mModules[1].mWords.end()),
                (std::vector<std::uint32_t>{ sMagic, 2 }));
            EXPECT_TRUE(set.mCensus);
            EXPECT_FALSE(ShaderFolder(folder, false).getSet().mCensus);

            constexpr std::array<std::uint32_t, 3> first{ sMagic, 1, 1 };
            constexpr std::array<std::uint32_t, 2> second{ sMagic, 2 };
            const std::array<ShaderModule, 2> inMemory{ { { "a.comp.spv", first }, { "b.comp.spv", second } } };
            EXPECT_EQ(set.mDigest, digestShaders(inMemory));
        }

        /// A folder that is not there, or a file in it that is no module, is refused rather than read as a set
        /// without it, which would fail at the first pipeline that names the module.
        TEST(RtxShaderFolderTest, aMissingFolderOrAFileThatIsNoModuleIsRefused)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            EXPECT_THROW(ShaderFolder(root / "missing", false), InputError);

            writeModule(root / "broken" / "a.spv", { 1, 2 });
            EXPECT_THROW(ShaderFolder(root / "broken", false), InputError);
        }

        /// **The game's compiled-in set is the stripped folder without the probes**, word for word, under the digest
        /// its modules have: the build's embedding step neither drops, reorders nor alters a module, and ships
        /// nothing only the tests run.
        TEST(RtxShaderFolderTest, theGamesSetIsTheStrippedFolderWithoutTheProbes)
        {
            const ShaderFolder folder(std::filesystem::path(OPENMW_RTX_SHADER_DIR), false);
            const ShaderSet& shipped = shippedVulkanShaders();
            EXPECT_FALSE(shipped.mCensus);
            EXPECT_EQ(shipped.mDigest, digestShaders(shipped.mModules));

            std::vector<std::string_view> left;
            for (const ShaderModule& module : folder.getSet().mModules)
            {
                const ShaderModule* const embedded = shipped.find(module.mName);
                if (embedded == nullptr)
                    left.push_back(module.mName);
                else
                    EXPECT_TRUE(std::ranges::equal(embedded->mWords, module.mWords)) << module.mName;
            }
            EXPECT_EQ(left,
                (std::vector<std::string_view>{ "halfmean.comp.spv", "halfstore.comp.spv", "pinning.comp.spv",
                    "probe.comp.spv", "shadowmoments.comp.spv", "share.comp.spv", "sharedexponent.comp.spv",
                    "traceprobe.rgen.spv", "unormmean.comp.spv", "unormround.comp.spv" }));
            EXPECT_EQ(shipped.mModules.size() + left.size(), folder.getSet().mModules.size());
        }
    }
}
