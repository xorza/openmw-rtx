#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/mirror/cells/cellsupply.hpp>
#include <components/rtx/mirror/cells/celltable.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>

namespace Rtx
{
    namespace
    {
        constexpr float sCell = 8192.0f;

        /// The eye at the middle of `cell`, its band one cell wide and its groundcover's under half a cell: the eye's
        /// cell, its four sides half a cell off and its four corners `sqrt(0.5) ≈ 0.71` of a cell off are within the
        /// band; a cell two off, 1.5 cells away, is not. The groundcover's holds the eye's cell alone.
        CellTarget targetAt(const osg::Vec2i& cell)
        {
            return CellTarget{
                .mGrid = CellGrid(sCell),
                .mEye = osg::Vec3f(
                    (static_cast<float>(cell.x()) + 0.5f) * sCell, (static_cast<float>(cell.y()) + 0.5f) * sCell, 0.0f),
                .mBand = sCell,
                .mKept = 2.0f * sCell,
                .mGrassBand = 0.4f * sCell,
                .mGrassKept = 1.4f * sCell,
                .mPredicted = {},
                .mActiveGrid = {},
            };
        }

        /// The band's nine cells in the order the reader takes them: the eye's, then its sides at a distance of a half
        /// squared, then its corners at a half squared on each axis, each run by the cell's own order.
        const std::array<osg::Vec2i, 9> sNearestFirst{ osg::Vec2i(0, 0), osg::Vec2i(-1, 0), osg::Vec2i(0, -1),
            osg::Vec2i(0, 1), osg::Vec2i(1, 0), osg::Vec2i(-1, -1), osg::Vec2i(-1, 1), osg::Vec2i(1, -1),
            osg::Vec2i(1, 1) };

        struct RtxCellTableTest : ::testing::Test
        {
            CellTable mTable;
            CellReturns mBack;
            CellsDropped mDropped;
            std::array<PreparedCell, 32> mCells;
            std::array<PreparedGrass, 4> mGrass;
            std::size_t mUsed = 0;

            /// Picks the next read, which is a cell's, and files it.
            osg::Vec2i readNextCell()
            {
                const std::optional<CellPick> pick = mTable.pick();
                EXPECT_TRUE(pick.has_value());
                EXPECT_EQ(pick->mKind, ReadKind::Cell);
                PreparedCell& read = mCells.at(mUsed++);
                read.mCell = pick->mCell;
                EXPECT_TRUE(mTable.file(*pick, read));
                return pick->mCell;
            }
        };

        /// **A band wants its cells nearest first, and the reader takes the groundcover first**: the eye's cell's
        /// groundcover, then the nine cells in `sNearestFirst`'s order, and nothing past the band.
        TEST_F(RtxCellTableTest, aBandWantsItsCellsNearestFirstAndTheReaderTakesTheGroundcoverFirst)
        {
            mTable.retarget(targetAt(osg::Vec2i(0, 0)), mBack, mDropped);

            const std::optional<CellPick> grass = mTable.pick();
            ASSERT_TRUE(grass.has_value());
            EXPECT_EQ(grass->mKind, ReadKind::Grass);
            EXPECT_EQ(grass->mCell, osg::Vec2i(0, 0));

            std::vector<osg::Vec2i> taken;
            while (const std::optional<CellPick> pick = mTable.pick())
            {
                EXPECT_EQ(pick->mKind, ReadKind::Cell);
                taken.push_back(pick->mCell);
            }
            EXPECT_EQ(taken, std::vector<osg::Vec2i>(sNearestFirst.begin(), sNearestFirst.end()));
            EXPECT_FALSE(mTable.canPick()) << "every read is in hand";
        }

        /// **The reader waits at `sReadAheadMost` reads the frame has not adopted**, and a read nearer than every one
        /// of its kind waiting is taken past it: eight of the nine filed, the ninth waits; one adopted, it is taken.
        /// The eye moved two cells east, its own new cell is nearer than every read waiting, and is taken at once.
        TEST_F(RtxCellTableTest, theReaderWaitsAtTheBoundButForAReadNearerThanEveryOneWaiting)
        {
            static_assert(CellTable::sReadAheadMost == 8);
            mTable.retarget(targetAt(osg::Vec2i(0, 0)), mBack, mDropped);
            const std::optional<CellPick> grass = mTable.pick();
            ASSERT_TRUE(grass.has_value());
            ASSERT_TRUE(mTable.file(*grass, mGrass[0]));

            // The groundcover is one of the eight.
            for (std::size_t at = 0; at < 7; ++at)
                EXPECT_EQ(readNextCell(), sNearestFirst[at]);
            EXPECT_FALSE(mTable.canPick()) << "eight wait, and the next is past every one of them";
            EXPECT_FALSE(mTable.pick().has_value());

            std::vector<PreparedCell*> cells;
            std::vector<PreparedGrass*> grasses;
            mTable.collectFiled(cells, grasses);
            ASSERT_EQ(cells.size(), 7u);
            ASSERT_EQ(grasses.size(), 1u);
            EXPECT_EQ(mTable.adoptCell(), cells[0]) << "the nearest read, adopted";
            EXPECT_EQ(readNextCell(), sNearestFirst[7]);
            EXPECT_FALSE(mTable.canPick());

            // From the middle of (2, 0): its own cell is fresh and nearest; (1, 0) is read at half a cell and (0, 0)
            // stands at 1.5 cells, within the two the band keeps.
            mTable.retarget(targetAt(osg::Vec2i(2, 0)), mBack, mDropped);
            const std::optional<CellPick> nearest = mTable.pick();
            ASSERT_TRUE(nearest.has_value());
            EXPECT_EQ(nearest->mCell, osg::Vec2i(2, 0));
            EXPECT_EQ(nearest->mKind, ReadKind::Grass) << "its groundcover first";
        }

        /// **What a band lets go of goes back by who knew it.** A read in hand as its band leaves is filed as no
        /// band's and given back by the reader; a read the frame counted goes to `dropped`, one it never counted
        /// straight back with its holds; a cell stood past the band's keep is no longer stood.
        TEST_F(RtxCellTableTest, whatABandLetsGoOfGoesBackByWhoKnewIt)
        {
            mTable.retarget(targetAt(osg::Vec2i(0, 0)), mBack, mDropped);
            ASSERT_TRUE(mTable.pick().has_value()) << "the groundcover";
            EXPECT_EQ(readNextCell(), osg::Vec2i(0, 0));
            EXPECT_EQ(readNextCell(), osg::Vec2i(-1, 0));
            const std::optional<CellPick> inHand = mTable.pick();
            ASSERT_TRUE(inHand.has_value());
            EXPECT_EQ(inHand->mCell, osg::Vec2i(0, -1));

            std::vector<PreparedCell*> cells;
            std::vector<PreparedGrass*> grasses;
            mTable.collectFiled(cells, grasses);
            ASSERT_EQ(cells.size(), 2u) << "(0, 0) and (-1, 0), counted";
            EXPECT_EQ(mTable.adoptCell(), cells[0]);
            EXPECT_EQ(mTable.countStood(ReadKind::Cell), 1u);
            EXPECT_EQ(readNextCell(), osg::Vec2i(0, 1)) << "filed after the count, so never counted";

            // Ten cells east, every cell of the old band is past what the new one keeps.
            mTable.retarget(targetAt(osg::Vec2i(10, 0)), mBack, mDropped);
            ASSERT_EQ(mDropped.mCells.size(), 1u);
            EXPECT_EQ(mDropped.mCells[0]->mCell, osg::Vec2i(-1, 0)) << "counted, so the frame's to give back";
            ASSERT_EQ(mBack.mCells.size(), 1u);
            EXPECT_EQ(mBack.mCells[0]->mCell, osg::Vec2i(0, 1)) << "never counted, so straight back";
            EXPECT_EQ(mTable.countStood(ReadKind::Cell), 0u) << "the stood cell, let go";

            PreparedCell late;
            late.mCell = inHand->mCell;
            EXPECT_FALSE(mTable.file(*inHand, late)) << "in hand as its band left, so the reader's to give back";
            EXPECT_EQ(mTable.findOwed(ReadKind::Cell), std::optional<std::size_t>(0)) << "the new band's nearest";
        }

        /// **A predicted band's cells are wanted after the eye's, and kept as the eye's are**: the eye at (0, 0) and a
        /// prediction at (10, 0), each band nine cells; the eye's nine are read first, then the prediction's, its own
        /// cell first. The prediction gone, its cells are wanted no more, and one read and adopted is let go of.
        TEST_F(RtxCellTableTest, aPredictedBandsCellsAreWantedAfterTheEyes)
        {
            CellTarget target = targetAt(osg::Vec2i(0, 0));
            target.mGrassBand = 0.0f;
            target.mGrassKept = 0.0f;
            target.mPredicted = { targetAt(osg::Vec2i(10, 0)).mEye };
            mTable.retarget(target, mBack, mDropped);

            std::vector<osg::Vec2i> taken;
            std::vector<PreparedCell*> cells;
            std::vector<PreparedGrass*> grasses;
            for (std::size_t at = 0; at < 2 * sNearestFirst.size(); ++at)
            {
                taken.push_back(readNextCell());
                mTable.collectFiled(cells, grasses);
                ASSERT_NE(mTable.adoptCell(), nullptr);
            }
            EXPECT_FALSE(mTable.canPick()) << "both bands read";
            for (std::size_t at = 0; at < sNearestFirst.size(); ++at)
            {
                EXPECT_EQ(taken[at], sNearestFirst[at]);
                EXPECT_EQ(taken[sNearestFirst.size() + at], sNearestFirst[at] + osg::Vec2i(10, 0));
            }

            target.mPredicted.clear();
            mTable.retarget(target, mBack, mDropped);
            EXPECT_EQ(mTable.countStood(ReadKind::Cell), 9u) << "the prediction's nine let go";
            EXPECT_TRUE(mDropped.mCells.empty()) << "stood, so the frame's placer lets go of them";
        }

        /// **A settled walk is owed the nearest read its band lacks**, within the band alone: nothing once the band
        /// stands, though a read kept past the band is still in hand.
        TEST_F(RtxCellTableTest, aSettledWalkIsOwedTheNearestReadItsBandLacks)
        {
            mTable.retarget(targetAt(osg::Vec2i(0, 0)), mBack, mDropped);
            ASSERT_TRUE(mTable.pick().has_value()) << "the groundcover";
            const std::optional<std::size_t> owed = mTable.findOwed(ReadKind::Cell);
            ASSERT_EQ(owed, std::optional<std::size_t>(0));
            EXPECT_FALSE(mTable.isRead(*owed, ReadKind::Cell));
            EXPECT_EQ(readNextCell(), osg::Vec2i(0, 0));
            EXPECT_TRUE(mTable.isRead(*owed, ReadKind::Cell));
            EXPECT_FALSE(mTable.isRead(0, ReadKind::Grass)) << "the groundcover is in hand";

            // Each adopted as it is read, so the reader never waits at the bound.
            std::vector<PreparedCell*> cells;
            std::vector<PreparedGrass*> grasses;
            for (std::size_t at = 0; at < sNearestFirst.size(); ++at)
            {
                if (at > 0)
                    EXPECT_EQ(readNextCell(), sNearestFirst[at]);
                mTable.collectFiled(cells, grasses);
                EXPECT_NE(mTable.adoptCell(), nullptr);
            }
            EXPECT_EQ(mTable.countStood(ReadKind::Cell), 9u);
            EXPECT_FALSE(mTable.findOwed(ReadKind::Cell).has_value()) << "the band stands whole";
            EXPECT_EQ(mTable.findOwed(ReadKind::Grass), std::optional<std::size_t>(0)) << "its groundcover in hand";
        }
    }
}
