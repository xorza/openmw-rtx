#pragma once

#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3f>
#include <osg/Vec4i>

#include <components/misc/constants.hpp>

namespace Rtx
{
    /// How far out the world is built, as the settings say it: `[RTX] distant land cells`, and the
    /// rasterizer's viewing distance, which a count of nought hands the decision back to. Cells and
    /// not units, because how wide a cell is is its worldspace's to say — `CellGrid::reachOf`.
    struct LandReach
    {
        float mCells = 0.0f;
        float mViewingDistance = 0.0f;

        bool operator==(const LandReach& other) const = default;
    };

    /// One worldspace's exterior grid: every question the rings ask of a cell, measured in that
    /// worldspace's own cells. A value and not a constant, because Morrowind's cells are 8192
    /// units wide and an ESM4 worldspace's 4096, and the land is read at the worldspace's size: a
    /// ring that measured by the other asked for the cell at half the eye's coordinates.
    class CellGrid
    {
    public:
        /// Morrowind's grid, which is every ESM3 worldspace's.
        CellGrid() = default;

        explicit CellGrid(float cellSize)
            : mCellSize(cellSize)
        {
        }

        /// One cell's side, in units.
        float getCellSize() const { return mCellSize; }

        /// The cell a world position stands in.
        osg::Vec2i cellOf(const osg::Vec3f& position) const;

        /// Whether some point of `cell` lies nearer than `radius` units to `eye` — the disc the rings
        /// are. Nearer and not as near, so that a cell touching the disc at its rim alone is out,
        /// which is what the bounding square `cellOf(eye ± radius)` cuts on the near side already.
        ///
        /// **A disc and not a square of cells, because the air that hides the rings' edge is a
        /// disc.** `fogEdgeOver` closes at the reach from the eye's own position, in every direction
        /// alike, so a cell whose nearest point is further than that is behind air that passes one
        /// part in 256 of it, and one nearer shows. A square of cells about the eye's cell loaded a
        /// corner cell that stood at 1.4 times the reach, wholly hidden, and along an axis up to a
        /// cell beyond the edge: at a reach of four that was twelve of eighty-one cells built,
        /// traced and never seen. Measured from the eye's position and not its cell, so what stands
        /// changes one cell at a time as the eye moves, which is the pace the ring adopts at anyway.
        bool withinReach(const osg::Vec2i& cell, const osg::Vec3f& eye, float radius) const;

        /// How far the eye stands from the nearest point of `cell`, squared, by the plane's own
        /// metric: the eye itself where it stands inside the cell's square, else the eye clamped to
        /// the square's sides. What `withinReach` measures, and what orders the cells a ring asks for.
        float distanceSquaredTo(const osg::Vec2i& cell, const osg::Vec3f& eye) const;

        /// The same distance by the square's own metric — the larger of the two axes' gaps, in
        /// units — which is the one the paging measured a chunk's reach by, and so what the size
        /// rule a placement is admitted under reads. Two metrics side by side because two callers
        /// were written against two engines: the disc is this fork's and the square is upstream's.
        float chebyshevDistanceTo(const osg::Vec2i& cell, const osg::Vec3f& eye) const;

        /// Calls `each(cell)` for every cell `withinReach` of `eye`, column by column and row by
        /// row: the disc's bounding square, and the disc out of it.
        template <class Each>
        void forEachCellWithin(const osg::Vec3f& eye, const float radius, Each&& each) const
        {
            const osg::Vec2i low = cellOf(eye - osg::Vec3f(radius, radius, 0.0f));
            const osg::Vec2i high = cellOf(eye + osg::Vec3f(radius, radius, 0.0f));
            for (int x = low.x(); x <= high.x(); ++x)
                for (int y = low.y(); y <= high.y(); ++y)
                {
                    const osg::Vec2i cell(x, y);
                    if (withinReach(cell, eye, radius))
                        each(cell);
                }
        }

        /// How far from the eye the world is built, in units: one number for the ground and the
        /// air, because an extinction tuned to a shorter distance swallows everything past the
        /// active grid. `reach.mCells` of this grid's cells, or the viewing distance where the
        /// count is nought.
        float reachOf(const LandReach& reach) const;

        bool operator==(const CellGrid& other) const = default;

    private:
        /// The eye's gap to the cell's square along each axis, nought inside it: what both metrics
        /// are taken over.
        osg::Vec2f gapsTo(const osg::Vec2i& cell, const osg::Vec3f& eye) const;

        float mCellSize = static_cast<float>(Constants::CellSizeInUnits);
    };

    /// Whether `cell` is inside the active grid `Terrain::World` states — minimum inclusive,
    /// maximum exclusive — which is the square the game has stood for itself and nothing out
    /// here may stand again.
    bool inActiveGrid(const osg::Vec2i& cell, const osg::Vec4i& activeGrid);
}
