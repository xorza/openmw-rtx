#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include <components/rtx/shaders/look.h>

namespace Rtx
{
    namespace
    {
        /// **A constant a transcendental derives is its derivation, correctly rounded**: each literal
        /// `portable.h` asks for, held to the expression its comment states, worked in double and
        /// rounded once to the float both sides read.
        TEST(RtxSharedConstantTest, eachLiteralIsItsDerivationCorrectlyRounded)
        {
            constexpr double pi = std::numbers::pi;
            EXPECT_EQ(Shaders::MAX_LOG_LUMINANCE,
                static_cast<float>(6.0 + std::log2(static_cast<double>(Shaders::DAYLIGHT_GAIN))));
            EXPECT_EQ(Shaders::FOG_EDGE_RISE, static_cast<float>(std::sin(25.0 * pi / 180.0)));
        }
    }
}
