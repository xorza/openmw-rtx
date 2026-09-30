#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2i>
#include <osg/Vec3f>
#include <osg/Vec4i>

#include <components/rtx/mirror/cells/cellgrid.hpp>

namespace Rtx
{
    namespace
    {
        /// Morrowind's cell width and an ESM4 worldspace's, which every rule below holds for alike.
        constexpr float sCellSizes[] = { 8192.0f, 4096.0f };

        /// The disc the rings stand is the one the air closes at: a cell is in reach where some
        /// point of it is nearer than the reach to the eye's own position.
        ///
        /// **Measured to the cell's nearest point and not to its centre or its index**, and
        /// strictly. A cell whose nearest point is exactly at the reach touches the closing air's
        /// rim and shows nothing of itself; and the bounding square `cellOf(eye - reach)` starts
        /// at the cell that holds that rim, so a rule that took the cell before it would count on
        /// one side what it could not walk on the other.
        TEST(RtxCellGridTest, aCellIsInReachWhereItsNearestPointIsNearerThanTheReach)
        {
            for (const float cell : sCellSizes)
            {
                SCOPED_TRACE(cell);
                const CellGrid grid(cell);

                // From the centre of cell (0, 0): a cell three over has its near side 2.5 cells away
                // and a cell four over 3.5, so both are in at four cells; the corner (3, 3) is
                // sqrt(2) * 2.5 = 3.54 away and in, and (4, 4) is 4.95 away and out.
                const osg::Vec3f centre(0.5f * cell, 0.5f * cell, 0.0f);
                EXPECT_EQ(grid.distanceSquaredTo(osg::Vec2i(0, 0), centre), 0.0f) << "the eye's own cell";
                EXPECT_FLOAT_EQ(grid.distanceSquaredTo(osg::Vec2i(3, 0), centre), 2.5f * 2.5f * cell * cell);
                EXPECT_FLOAT_EQ(grid.distanceSquaredTo(osg::Vec2i(-3, 0), centre), 2.5f * 2.5f * cell * cell)
                    << "symmetric";
                EXPECT_FLOAT_EQ(grid.distanceSquaredTo(osg::Vec2i(3, 3), centre), 2.0f * 2.5f * 2.5f * cell * cell);
                EXPECT_TRUE(grid.withinReach(osg::Vec2i(4, 0), centre, 4.0f * cell));
                EXPECT_TRUE(grid.withinReach(osg::Vec2i(3, 3), centre, 4.0f * cell));
                EXPECT_FALSE(grid.withinReach(osg::Vec2i(4, 4), centre, 4.0f * cell)) << "a corner the square took";
                EXPECT_FALSE(grid.withinReach(osg::Vec2i(5, 0), centre, 4.0f * cell)) << "4.5 cells away";

                // From a cell corner, the rim: cell (6, 0) begins exactly six cells away and cell
                // (-7, 0) ends there, and neither is nearer than six.
                const osg::Vec3f corner;
                EXPECT_FLOAT_EQ(grid.distanceSquaredTo(osg::Vec2i(6, 0), corner), 36.0f * cell * cell);
                EXPECT_FLOAT_EQ(grid.distanceSquaredTo(osg::Vec2i(-7, 0), corner), 36.0f * cell * cell);
                EXPECT_FALSE(grid.withinReach(osg::Vec2i(6, 0), corner, 6.0f * cell));
                EXPECT_FALSE(grid.withinReach(osg::Vec2i(-7, 0), corner, 6.0f * cell));
                EXPECT_TRUE(grid.withinReach(osg::Vec2i(5, 0), corner, 6.0f * cell));
                EXPECT_TRUE(grid.withinReach(osg::Vec2i(-6, 0), corner, 6.0f * cell));

                // And the eye's height plays no part: the rings are a disc on the ground.
                EXPECT_EQ(grid.distanceSquaredTo(osg::Vec2i(3, 0), centre + osg::Vec3f(0.0f, 0.0f, 5000.0f)),
                    grid.distanceSquaredTo(osg::Vec2i(3, 0), centre));
            }
        }

        /// Which cell a position stands in is the floor of both axes, so the seam between two
        /// cells belongs to the one above it and a negative coordinate rounds towards minus
        /// infinity rather than towards nought.
        TEST(RtxCellGridTest, aPositionStandsInTheCellBelowItOnBothAxes)
        {
            for (const float cell : sCellSizes)
            {
                SCOPED_TRACE(cell);
                const CellGrid grid(cell);
                EXPECT_EQ(grid.cellOf(osg::Vec3f(0.0f, 0.0f, 0.0f)), osg::Vec2i(0, 0));
                EXPECT_EQ(grid.cellOf(osg::Vec3f(cell - 1.0f, 0.0f, 0.0f)), osg::Vec2i(0, 0));
                EXPECT_EQ(grid.cellOf(osg::Vec3f(cell, 0.0f, 0.0f)), osg::Vec2i(1, 0)) << "the seam is the cell above";
                EXPECT_EQ(grid.cellOf(osg::Vec3f(-1.0f, -1.0f, 0.0f)), osg::Vec2i(-1, -1))
                    << "not truncated towards nought";
                EXPECT_EQ(grid.cellOf(osg::Vec3f(2.5f * cell, -3.5f * cell, 9000.0f)), osg::Vec2i(2, -4))
                    << "and the height plays no part";
            }
        }

        /// The walk over the disc visits every cell `withinReach` says is in and no other, in
        /// column-major order, so two runs from one eye walk one list.
        TEST(RtxCellGridTest, theWalkOverTheDiscIsExactlyTheCellsInReachInOneOrder)
        {
            for (const float cell : sCellSizes)
            {
                SCOPED_TRACE(cell);
                const CellGrid grid(cell);
                const osg::Vec3f eye(0.5f * cell, 0.5f * cell, 0.0f);
                const float reach = 2.0f * cell;

                std::vector<osg::Vec2i> walked;
                grid.forEachCellWithin(eye, reach, [&](const osg::Vec2i& at) { walked.push_back(at); });

                // Every cell of the bounding square that is in reach, by hand: the square runs from
                // (-2, -2) to (2, 2), and of its 25 cells the four corners stand sqrt(2) * 1.5 = 2.12
                // cells away and are out.
                std::vector<osg::Vec2i> expected;
                for (int x = -2; x <= 2; ++x)
                    for (int y = -2; y <= 2; ++y)
                        if (!((x == -2 || x == 2) && (y == -2 || y == 2)))
                            expected.emplace_back(x, y);

                EXPECT_EQ(walked, expected);
                EXPECT_EQ(walked.size(), std::size_t{ 21 });
            }
        }

        /// The paging measured a chunk by the larger of the two axes' gaps, and the size rule a
        /// placement is admitted under reads that same number: shorter than the plane's distance
        /// wherever a cell stands off both axes, and the same where it stands on one.
        TEST(RtxCellGridTest, theSquaresOwnDistanceIsTheLargerAxisAndNeverLongerThanThePlanes)
        {
            for (const float cell : sCellSizes)
            {
                SCOPED_TRACE(cell);
                const CellGrid grid(cell);
                const osg::Vec3f centre(0.5f * cell, 0.5f * cell, 0.0f);

                EXPECT_EQ(grid.chebyshevDistanceTo(osg::Vec2i(0, 0), centre), 0.0f);
                EXPECT_FLOAT_EQ(grid.chebyshevDistanceTo(osg::Vec2i(3, 0), centre), 2.5f * cell);
                EXPECT_FLOAT_EQ(grid.chebyshevDistanceTo(osg::Vec2i(3, 3), centre), 2.5f * cell)
                    << "the corner is as far as the side, by the square's metric";
                EXPECT_FLOAT_EQ(grid.chebyshevDistanceTo(osg::Vec2i(-3, 2), centre), 2.5f * cell);

                // Against the plane's: equal on an axis, shorter off it.
                const float side = grid.chebyshevDistanceTo(osg::Vec2i(3, 0), centre);
                EXPECT_FLOAT_EQ(side * side, grid.distanceSquaredTo(osg::Vec2i(3, 0), centre));
                const float corner = grid.chebyshevDistanceTo(osg::Vec2i(3, 3), centre);
                EXPECT_LT(corner * corner, grid.distanceSquaredTo(osg::Vec2i(3, 3), centre));
            }
        }

        /// The active grid is stated as `Terrain::World` states it — minimum inclusive, maximum
        /// exclusive — and a cell on the far edge is outside it.
        TEST(RtxCellGridTest, theActiveGridIsInclusiveBelowAndExclusiveAbove)
        {
            const osg::Vec4i grid(-1, -1, 2, 2);

            EXPECT_TRUE(inActiveGrid(osg::Vec2i(-1, -1), grid));
            EXPECT_TRUE(inActiveGrid(osg::Vec2i(1, 1), grid));
            EXPECT_TRUE(inActiveGrid(osg::Vec2i(0, 0), grid));
            EXPECT_FALSE(inActiveGrid(osg::Vec2i(2, 0), grid)) << "the maximum is exclusive";
            EXPECT_FALSE(inActiveGrid(osg::Vec2i(0, 2), grid));
            EXPECT_FALSE(inActiveGrid(osg::Vec2i(-2, 0), grid));
            EXPECT_FALSE(inActiveGrid(osg::Vec2i(0, -2), grid));
        }

        /// The reach is the setting's cells of the grid's own width, and nought hands the decision
        /// to the rasterizer's viewing distance: four cells is 32768 units in Morrowind's world and
        /// 16384 in a worldspace of 4096-unit cells.
        TEST(RtxCellGridTest, theReachIsCellsOfTheGridsWidthOrTheViewingDistanceWhereNoneWereNamed)
        {
            EXPECT_FLOAT_EQ(CellGrid().reachOf({ .mCells = 4.0f, .mViewingDistance = 7168.0f }), 32768.0f);
            EXPECT_FLOAT_EQ(CellGrid(4096.0f).reachOf({ .mCells = 4.0f, .mViewingDistance = 7168.0f }), 16384.0f);
            EXPECT_FLOAT_EQ(CellGrid().reachOf({ .mCells = 0.5f, .mViewingDistance = 7168.0f }), 4096.0f);
            EXPECT_FLOAT_EQ(CellGrid().reachOf({ .mCells = 0.0f, .mViewingDistance = 7168.0f }), 7168.0f);
            EXPECT_FLOAT_EQ(CellGrid(4096.0f).reachOf({ .mCells = -1.0f, .mViewingDistance = 7168.0f }), 7168.0f)
                << "a negative count is none";
        }

        /// **The grid is the worldspace's.** An eye at x = 40960 stands in cell 5 of Morrowind's
        /// grid and in cell 10 of a grid of 4096-unit cells, and the land of cell 10 is what the
        /// reader builds under it there: a ring that measured by the fixed width asked for cell 5,
        /// whose land stands at x = 20480.
        TEST(RtxCellGridTest, anEyeStandsInTheCellOfItsWorldspacesOwnWidth)
        {
            const osg::Vec3f eye(40960.0f, 0.0f, 0.0f);
            EXPECT_EQ(CellGrid().cellOf(eye), osg::Vec2i(5, 0));
            EXPECT_EQ(CellGrid(4096.0f).cellOf(eye), osg::Vec2i(10, 0));
            EXPECT_EQ(CellGrid().getCellSize(), 8192.0f) << "Morrowind's, by default";
        }
    }
}
