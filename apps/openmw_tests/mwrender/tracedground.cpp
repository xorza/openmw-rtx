#include <cstdint>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/tracedground.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/position.hpp>
#include <components/esm3/loadcell.hpp>

namespace MWRender
{
    namespace
    {
        ESM::Position at(float x, float y)
        {
            ESM::Position position{};
            position.pos[0] = x;
            position.pos[1] = y;
            return position;
        }

        /// **A paged reference has gone away where it stands in another cell than its content puts
        /// it in**, on the default worldspace's grid of 8192 units a cell: 8100 and 8191 are cell 0
        /// on either axis, 8192 cell 1 and -1 cell -1. A move inside its cell, past where it was
        /// placed, has gone nowhere; across an edge on either axis it has; and in an interior or
        /// another worldspace it has, whatever its coordinates read as here.
        TEST(RtxTracedGroundTest, aReferenceIsAwayOnlyInAnotherCellThanItsContentPutsItIn)
        {
            const ESM::RefId world = ESM::Cell::sDefaultWorldspaceId;
            EXPECT_FALSE(standsAway(true, at(8191.0f, 10.0f), at(8100.0f, 10.0f), world));
            EXPECT_TRUE(standsAway(true, at(8192.0f, 10.0f), at(8100.0f, 10.0f), world));
            EXPECT_TRUE(standsAway(true, at(10.0f, -1.0f), at(10.0f, 10.0f), world));
            EXPECT_FALSE(standsAway(true, at(-1.0f, -1.0f), at(-8192.0f, -8192.0f), world));
            EXPECT_TRUE(standsAway(false, at(8100.0f, 10.0f), at(8100.0f, 10.0f), world));
        }

        /// **An actor's move is no reference the ring holds**, nor a levelled list's, in either format; a static's, a
        /// door's and a lamp's are.
        TEST(RtxTracedGroundTest, anActorsMoveIsNoneTheRingHolds)
        {
            // As the world hands them over, `MWWorld::ESMStore::find`'s `std::int32_t`.
            const auto asFound = [](const ESM::RecNameInts type) { return static_cast<std::int32_t>(type); };
            for (const ESM::RecNameInts actor : { ESM::REC_NPC_, ESM::REC_CREA, ESM::REC_LEVC, ESM::REC_NPC_4,
                     ESM::REC_CREA4, ESM::REC_LVLC4, ESM::REC_LVLN4 })
                EXPECT_FALSE(ringMayHold(asFound(actor))) << actor;
            for (const ESM::RecNameInts held : { ESM::REC_STAT, ESM::REC_DOOR, ESM::REC_LIGH, ESM::REC_STAT4 })
                EXPECT_TRUE(ringMayHold(asFound(held))) << held;
        }
    }
}
