#include <optional>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/skystate.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender
{
    namespace
    {
        /// **The weather's names stand on the sky's own copies, made only where a name changed.**
        /// Each of the two views is first set on a weather record's name that then goes, as a
        /// global script's write frees it: after `keepNames` each still reads its name, from
        /// `mNames`. A second `keepNames` over the same names leaves each copy where it is, so a
        /// frame copies nothing; a changed name is copied, and an emptied one is empty.
        TEST(MWRenderSkyStateTest, theWeathersNamesStandOnTheSkysOwnCopiesMadeWhereTheyChanged)
        {
            SkyState sky;
            {
                std::optional<VFS::Path::Normalized> record[2]{ VFS::Path::Normalized("textures/tx_sky_cloudy.dds"),
                    VFS::Path::Normalized("textures/tx_sky_overcast.dds") };
                sky.mWeather.mCloudTexture = *record[0];
                sky.mWeather.mNextCloudTexture = *record[1];
                sky.keepNames();
                for (std::optional<VFS::Path::Normalized>& name : record)
                    name.reset();
            }

            EXPECT_EQ(sky.mWeather.mCloudTexture, "textures/tx_sky_cloudy.dds");
            EXPECT_EQ(sky.mWeather.mNextCloudTexture, "textures/tx_sky_overcast.dds");
            EXPECT_EQ(sky.mWeather.mNextCloudTexture.value().data(), sky.mNames.mNextCloudTexture.value().data())
                << "the view stands on the sky's copy";

            const char* const kept = sky.mNames.mCloudTexture.value().data();
            sky.keepNames();
            EXPECT_EQ(sky.mNames.mCloudTexture.value().data(), kept) << "an unchanged name was copied again";

            const VFS::Path::Normalized clear("textures/tx_sky_clear.dds");
            sky.mWeather.mCloudTexture = clear;
            sky.mWeather.mNextCloudTexture = VFS::Path::NormalizedView();
            sky.keepNames();
            EXPECT_EQ(sky.mWeather.mCloudTexture, "textures/tx_sky_clear.dds");
            EXPECT_EQ(sky.mWeather.mCloudTexture.value().data(), sky.mNames.mCloudTexture.value().data());
            EXPECT_TRUE(sky.mWeather.mNextCloudTexture.empty()) << "an emptied name stays empty";
        }
    }
}
