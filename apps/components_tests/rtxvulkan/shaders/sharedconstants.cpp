#include <cmath>

#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/glare.h>
#include <components/rtxvulkan/shaders/shared/indirect.h>

namespace Rtx
{
    namespace
    {
        /// The backend's own literals that a transcendental derives, held as
        /// `RtxSharedConstantTest.eachLiteralIsItsDerivationCorrectlyRounded` holds the core's.
        TEST(RtxIndirectLimitTest, theCeilingFollowsExposureAndNotTheRayCount)
        {
            for (const unsigned rays : { 1u, 4u, 16u })
                for (const float exposure : { 0.25f, 1.0f, 8.0f })
                {
                    const float sample = 32.0f / (exposure * rays);
                    const float scale = Shaders::indirectSampleScale(sample, 4.0f, exposure, rays);
                    EXPECT_FLOAT_EQ(sample * scale * rays * exposure, 4.0f);
                    EXPECT_EQ(Shaders::indirectSampleScale(sample, 0.0f, exposure, rays), 1.0f);
                    EXPECT_EQ(Shaders::indirectSampleScale(0.5f / (exposure * rays), 4.0f, exposure, rays), 1.0f);
                    EXPECT_EQ(Shaders::indirectSampleScale(0.0f, 4.0f, exposure, rays), 1.0f);
                }
        }

        TEST(RtxSharedConstantTest, eachBackendLiteralIsItsDerivationCorrectlyRounded)
        {
            EXPECT_EQ(Shaders::SUN_GLARE_QUERY_RADIUS, static_cast<float>(std::atan(225.0 * 17.0 / 64.0 / 1000.0)));
        }
    }
}
