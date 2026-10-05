#include <cmath>

#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/glare.h>

namespace Rtx
{
    namespace
    {
        /// The backend's own literals that a transcendental derives, held as
        /// `RtxSharedConstantTest.eachLiteralIsItsDerivationCorrectlyRounded` holds the core's.
        TEST(RtxSharedConstantTest, eachBackendLiteralIsItsDerivationCorrectlyRounded)
        {
            EXPECT_EQ(Shaders::SUN_GLARE_QUERY_RADIUS, static_cast<float>(std::atan(225.0 * 17.0 / 64.0 / 1000.0)));
        }
    }
}
