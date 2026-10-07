#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include <spirv/unified1/spirv.hpp>

#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/shaders/shared/pane.h>
#include <components/rtxvulkan/shaders/shared/sets.h>
#include <components/rtxvulkan/spirv/spirvbindings.hpp>
#include <components/rtxvulkan/spirv/spirvfile.hpp>

namespace Rtx
{
    namespace
    {
        std::vector<ModuleBinding> bindingsOf(const char* module)
        {
            std::vector<ModuleBinding> bindings;
            readBindings(readSpirv(std::filesystem::path(OPENMW_RTX_SHADER_DIR) / module), bindings);
            return bindings;
        }

        /// **A module's bindings are read as its GLSL declares them**: the pane filter's seven images
        /// in its pass's set, one each, read and written as storage — and nothing else in that set
        /// but the census, where the build counts.
        TEST(RtxSpirvBindingsTest, thePaneFiltersSevenStorageImagesAreReadOffItsModule)
        {
            std::vector<ModuleBinding> pass;
            for (const ModuleBinding& bound : bindingsOf("pane.comp.spv"))
                if (bound.mSet == Shaders::SET_PASS && bound.mBinding != Shaders::BIND_CENSUS)
                    pass.push_back(bound);
            std::ranges::sort(pass, {}, &ModuleBinding::mBinding);

            ASSERT_EQ(pass.size(), std::size_t{ Shaders::PANE_BINDINGS });
            for (std::uint32_t at = 0; at < Shaders::PANE_BINDINGS; ++at)
                EXPECT_EQ(pass[at],
                    (ModuleBinding{ .mSet = Shaders::SET_PASS,
                        .mBinding = at,
                        .mKind = DescriptorKind::StorageImage,
                        .mCount = 1 }))
                    << "binding " << at;
        }

        /// **The trace's own kinds read as theirs**: the scene's top-level structure, and the
        /// bindless textures as an array of no stated length, which is a count of nought.
        TEST(RtxSpirvBindingsTest, aStructureAndAnArrayOfNoLengthAreReadAsWhatTheyAre)
        {
            const std::vector<ModuleBinding> bindings = bindingsOf("visibility.rgen.spv");
            EXPECT_TRUE(std::ranges::any_of(bindings, [](const ModuleBinding& bound) {
                return bound.mKind == DescriptorKind::AccelerationStructure && bound.mCount == 1;
            })) << "no top-level structure";
            EXPECT_TRUE(std::ranges::any_of(bindings, [](const ModuleBinding& bound) {
                return bound.mKind == DescriptorKind::CombinedImageSampler && bound.mCount == 0;
            })) << "no array of textures of no length";

            const std::vector<std::uint32_t> notAModule{ 1u, 2u, 3u, 4u, 5u };
            std::vector<ModuleBinding> into;
            EXPECT_THROW(readBindings(notAModule, into), InputError);

            // **A module cut short is refused as the installation's**, as its file is: the pane
            // filter's words to the first word of its first instruction longer than one, whose length
            // then runs past the end.
            std::vector<std::uint32_t> cut = readSpirv(std::filesystem::path(OPENMW_RTX_SHADER_DIR) / "pane.comp.spv");
            std::size_t at = 5;
            while ((cut[at] >> 16) < 2)
                at += cut[at] >> 16;
            cut.resize(at + 1);
            EXPECT_THROW(readBindings(cut, into), InputError);

            // **An instruction shorter than its opcode is refused, and not read past**: a module that
            // ends in a decoration of a set with no set in it, a type with no id, and a variable with
            // no id after its type, each a whole instruction the module holds.
            const std::vector<std::uint32_t> header{ spv::MagicNumber, 0x00010600u, 0u, 1u, 0u };
            const std::uint32_t set = spv::DecorationDescriptorSet;
            for (const std::vector<std::uint32_t>& last :
                { std::vector<std::uint32_t>{ (3u << 16) | spv::OpDecorate, 1u, set },
                    std::vector<std::uint32_t>{ (1u << 16) | spv::OpTypeStruct },
                    std::vector<std::uint32_t>{ (2u << 16) | spv::OpVariable, 7u } })
            {
                std::vector<std::uint32_t> shortened = header;
                shortened.insert(shortened.end(), last.begin(), last.end());
                EXPECT_THROW(readBindings(shortened, into), InputError) << "opcode " << (last[0] & 0xffffu);
            }
            std::vector<std::uint32_t> whole = header;
            whole.insert(whole.end(), { (4u << 16) | spv::OpDecorate, 1u, set, 0u });
            EXPECT_NO_THROW(readBindings(whole, into)) << "a decoration of a set, whole";
        }
    }
}
