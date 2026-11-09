#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/rtxsettings.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/shaders/gbuffer.h>

namespace MWRender
{
    namespace
    {
        /// A set every field of which differs from its default, so a field the derivation drops
        /// or swaps with another shows.
        RtxSettingValues valid()
        {
            return RtxSettingValues{
                .mUpscale = Rtx::Upscale::Balanced,
                .mIndirectLighting = false,
                .mBounceRays = 3,
                .mDistantLandCells = 6.0f,
                .mViewingDistance = 7168.0f,
                .mObjectPaging = false,
                .mObjectPagingMinSize = 0.025f,
                .mGroundcover = true,
                .mGroundcoverDistance = 3072.0f,
                .mGroundcoverDensity = 0.5f,
                .mGroundcoverPointLighting = false,
                .mAnisotropy = 8,
                .mGamma = 1.5f,
                .mLitEnvironmentMaps = true,
            };
        }

        /// **Each value reaches its setting, and the cells mean the reach**, for the game and the
        /// harness alike, since both derive through this.
        TEST(RtxSettingsTest, eachValueReachesItsSettingAndTheCellsTheReach)
        {
            const RtxSettings derived = RtxSettings::derive(valid());
            EXPECT_EQ(derived.mUpscale, Rtx::Upscale::Balanced);
            EXPECT_FALSE(derived.mIndirectLighting);
            EXPECT_EQ(derived.mBounceRays, 3u);
            // Cells and the viewing distance, which a worldspace's grid turns into units: six of
            // Morrowind's 8192 are 49152.
            EXPECT_EQ(derived.mMirror.mReach, (Rtx::LandReach{ .mCells = 6.0f, .mViewingDistance = 7168.0f }));
            EXPECT_EQ(Rtx::CellGrid().reachOf(derived.mMirror.mReach), 49152.0f);
            EXPECT_FALSE(derived.mMirror.mDistantStatics);
            EXPECT_EQ(derived.mMirror.mMinSize, 0.025f);
            EXPECT_EQ(derived.mMirror.mGroundcoverReach, 3072.0f);
            EXPECT_EQ(derived.mMirror.mGroundcoverDensity, 0.5f);
            EXPECT_FALSE(derived.mMirror.mGroundcoverLampLit);
            EXPECT_EQ(derived.mAnisotropy, 8u);
            EXPECT_EQ(derived.mGamma, 1.5f);
            EXPECT_TRUE(derived.mLitEnvironmentMaps);

            RtxSettingValues handedBack = valid();
            handedBack.mDistantLandCells = 0.0f;
            EXPECT_EQ(Rtx::CellGrid().reachOf(RtxSettings::derive(handedBack).mMirror.mReach), 7168.0f)
                << "nought cells hands the reach to the viewing distance";

            RtxSettingValues bare = valid();
            bare.mGroundcover = false;
            EXPECT_EQ(RtxSettings::derive(bare).mMirror.mGroundcoverReach, 0.0f)
                << "groundcover switched off stands none, whatever its distance";

            RtxSettingValues unfiltered = valid();
            unfiltered.mAnisotropy = 0;
            EXPECT_EQ(RtxSettings::derive(unfiltered).mAnisotropy, 1u) << "nought filters as one does";
        }

        /// A gamma that is not a finite number over nought is refused: nought and less raise the
        /// picture to no power or a negative one, and an infinite one to the power of nought, which
        /// is white. The registry holds none of them; a harness's line holds anything.
        TEST(RtxSettingsTest, aGammaThatIsNotOverNoughtIsRefused)
        {
            for (const float gamma :
                { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
            {
                RtxSettingValues values = valid();
                values.mGamma = gamma;
                EXPECT_THROW(RtxSettings::derive(values), Rtx::InputError) << gamma;
            }

            RtxSettingValues least = valid();
            least.mGamma = std::numeric_limits<float>::min();
            EXPECT_EQ(RtxSettings::derive(least).mGamma, std::numeric_limits<float>::min())
                << "anything over nought stands";
        }

        /// A count of bounce rays outside one to `MAX_BOUNCE_RAYS` is refused: past the top there is no
        /// landing's list to run over, and no bounce at all is `[RTX] indirect lighting`'s to say. The
        /// registry clamps to the range; a harness's line holds anything.
        TEST(RtxSettingsTest, aCountOfBounceRaysOutsideItsRangeIsRefused)
        {
            constexpr int most = static_cast<int>(Rtx::Shaders::MAX_BOUNCE_RAYS);
            for (const int rays : { 0, -1, most + 1 })
            {
                RtxSettingValues values = valid();
                values.mBounceRays = rays;
                EXPECT_THROW(RtxSettings::derive(values), Rtx::InputError) << rays;
            }

            for (const int rays : { 1, most })
            {
                RtxSettingValues values = valid();
                values.mBounceRays = rays;
                EXPECT_EQ(RtxSettings::derive(values).mBounceRays, static_cast<std::uint32_t>(rays)) << rays;
            }
        }
    }
}
