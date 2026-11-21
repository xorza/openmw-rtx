#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/framedescriber.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <components/esm3/loadcell.hpp>

namespace MWRender
{
    namespace
    {
        /// **A cell's place, as a frame and a wait under the loading screen describe it alike**: outdoors with no room,
        /// a room under a sky with none either, and a room with its record's light and fog. A wait describes the cell
        /// its change arrived in by this, so it stands that cell's world whatever cell the scene still names (a first
        /// load went outdoors described as the room of no cell at all, and stood its cells a frame at a time).
        TEST(FrameDescriberTest, aCellsPlaceIsItsKindWithARoomsFactsOffItsRecord)
        {
            const ESM::Cell::AMBIstruct mood{
                .mAmbient = 0x102030, .mSunlight = 0x405060, .mFog = 0x708090, .mFogDensity = 0.75f
            };
            struct Case
            {
                std::string_view mName;
                std::int32_t mFlags;
                Location mLocation;
                bool mRoom;
            };
            constexpr std::array cases{
                Case{ "exterior", 0, Location::Exterior, false },
                Case{ "quasi-exterior", ESM::Cell::Interior | ESM::Cell::QuasiEx, Location::QuasiExterior, false },
                Case{ "interior", ESM::Cell::Interior, Location::Interior, true },
            };
            for (const Case& expected : cases)
            {
                SCOPED_TRACE(expected.mName);
                ESM::Cell record;
                record.mData.mFlags = expected.mFlags;
                record.mAmbi = mood;
                const FramePlace place = placeOf(MWWorld::Cell(record));
                EXPECT_EQ(place.mLocation, expected.mLocation);
                ASSERT_EQ(place.mRoom.has_value(), expected.mRoom);
                if (!expected.mRoom)
                    continue;
                EXPECT_EQ(place.mRoom->mAmbient, mood.mAmbient);
                EXPECT_EQ(place.mRoom->mSunlight, mood.mSunlight);
                EXPECT_EQ(place.mRoom->mFog, mood.mFog);
                EXPECT_EQ(place.mRoom->mFogDensity, mood.mFogDensity);
            }
        }
    }
}
