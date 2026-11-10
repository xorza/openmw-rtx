#include <cstddef>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        /// **A frame of one bounce ray holds the memory one bounce did**: the landing's own stage images
        /// stand a texel wide, since its one ray stores its landing in the solid's place
        /// (`landingApart`), and at the frame's stored size from two rays on; every other stage image
        /// stands at that size whatever the count. Sixteen wide is stored sixteen wide, an even width's
        /// two fields side by side (`storedWidth`).
        TEST(RtxGBufferStagesTest, onlyAFrameOfSeveralBounceRaysHoldsTheLandingsOwnImages)
        {
            std::vector<ArenaImage> one;
            std::vector<ArenaImage> two;
            GBuffer::declareStages(16, 8, 1, one);
            GBuffer::declareStages(16, 8, 2, two);
            ASSERT_EQ(one.size(), GBuffer::sStageImages);
            ASSERT_EQ(two.size(), GBuffer::sStageImages);

            std::size_t landings = 0;
            for (std::size_t at = 0; at < one.size(); ++at)
            {
                const bool landing = one[at].mName.starts_with("stage landing");
                landings += landing ? 1 : 0;
                EXPECT_EQ(one[at].mDescription.mWidth, landing ? 1u : 16u) << one[at].mName;
                EXPECT_EQ(one[at].mDescription.mHeight, landing ? 1u : 8u) << one[at].mName;
                EXPECT_EQ(two[at].mDescription.mWidth, 16u) << two[at].mName;
                EXPECT_EQ(two[at].mDescription.mHeight, 8u) << two[at].mName;
            }
            EXPECT_EQ(landings, 3u) << "the landing's hit, its ray's start and its direction";
        }
    }
}
