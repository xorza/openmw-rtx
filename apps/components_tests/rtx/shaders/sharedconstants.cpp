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

        /// **A constant whose arithmetic rounds on inexact literals is the device's number**: worked in
        /// double from the decimals its comment states, as glslang folds it, and rounded once.
        TEST(RtxSharedConstantTest, eachArithmeticLiteralIsWhatTheShaderCompilerFoldsItTo)
        {
            const double reflected = (1.333 - 1.0) / (1.333 + 1.0);
            EXPECT_EQ(Shaders::WATER_F0, static_cast<float>(reflected * reflected));
            EXPECT_EQ(Shaders::MAX_SUN_RADIANCE, static_cast<float>(1.0 / (0.05 / 10.0 * (reflected * reflected))));

            const double metre = 69.99125;
            EXPECT_EQ(Shaders::WATER_EXTINCTION.x(), static_cast<float>(0.262 / metre));
            EXPECT_EQ(Shaders::WATER_EXTINCTION.y(), static_cast<float>(0.059 / metre));
            EXPECT_EQ(Shaders::WATER_EXTINCTION.z(), static_cast<float>(0.024 / metre));

            EXPECT_EQ(Shaders::WATER_SCATTER_SHIPPED.x(), static_cast<float>(12.0 / 255.0 * 0.85));
            EXPECT_EQ(Shaders::WATER_SCATTER_SHIPPED.y(), static_cast<float>(30.0 / 255.0 * 0.85));
            EXPECT_EQ(Shaders::WATER_SCATTER_SHIPPED.z(), static_cast<float>(37.0 / 255.0 * 0.85));

            EXPECT_EQ(Shaders::WATER_CAUSTIC_SPREAD, static_cast<float>(2.0 * 0.004654 / 1.333));
            EXPECT_EQ(Shaders::WATER_IOR, 1.333f) << "the derivations above restate it";
            EXPECT_EQ(Shaders::UNITS_PER_METRE, 69.99125f) << "the derivations above restate it";
            EXPECT_EQ(Shaders::SUN_ANGULAR_RADIUS, 0.004654f) << "the derivations above restate it";
        }
    }
}
