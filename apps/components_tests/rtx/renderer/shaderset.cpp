#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtx/renderer/shaderset.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<std::uint32_t, 3> sFirst{ 1, 2, 3 };
        constexpr std::array<std::uint32_t, 3> sEdited{ 1, 2, 7 };
        constexpr std::array<std::uint32_t, 1> sSecond{ 4 };

        /// **The digest is the modules and nothing else.** The same modules are the same set; one word, or a
        /// module under another name, is another set.
        TEST(RtxShaderSetTest, theDigestIsEveryModulesNameAndWords)
        {
            const std::array<ShaderModule, 2> here{ { { "a.spv", sFirst }, { "b.spv", sSecond } } };
            const std::array<ShaderModule, 2> again{ { { "a.spv", sFirst }, { "b.spv", sSecond } } };
            const std::array<ShaderModule, 2> edited{ { { "a.spv", sEdited }, { "b.spv", sSecond } } };
            const std::array<ShaderModule, 2> renamed{ { { "a.spv", sFirst }, { "c.spv", sSecond } } };
            const std::array<ShaderModule, 2> traded{ { { "a.spv", sSecond }, { "b.spv", sFirst } } };

            const Misc::DigestWords digest = digestShaders(here);
            EXPECT_EQ(digestShaders(again), digest);
            EXPECT_NE(digestShaders(edited), digest);
            EXPECT_NE(digestShaders(renamed), digest);
            EXPECT_NE(digestShaders(traded), digest);
            EXPECT_NE(digest, (Misc::DigestWords{ 0, 0 })) << "two modules digested to nothing";
        }

        /// A module is found by its whole name, and a name the set does not hold, before, between or after its
        /// modules, finds none.
        TEST(RtxShaderSetTest, aModuleIsFoundByItsWholeName)
        {
            const std::array<ShaderModule, 2> modules{ { { "b.spv", sFirst }, { "d.spv", sSecond } } };
            const ShaderSet set{ .mModules = modules };

            ASSERT_NE(set.find("b.spv"), nullptr);
            EXPECT_EQ(set.find("b.spv")->mWords.data(), sFirst.data());
            ASSERT_NE(set.find("d.spv"), nullptr);
            EXPECT_EQ(set.find("d.spv")->mWords.data(), sSecond.data());
            for (const char* const absent : { "a.spv", "c.spv", "e.spv", "b", "b.spv2", "" })
                EXPECT_EQ(set.find(absent), nullptr) << absent;
            EXPECT_EQ(ShaderSet{}.find("b.spv"), nullptr);
        }
    }
}
