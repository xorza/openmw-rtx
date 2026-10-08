#include "lightgrid.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <osg/BoundingBox>
#include <osg/Vec3d>

namespace
{
    /// How many cells what the lamps reach may span, and how many lamp entries the grid may hold
    /// across all of its cells. Two budgets, because a wide exterior overruns the first and one
    /// lamp with an enormous reach overruns the second, and doubling the cell until both fit
    /// recovers either.
    ///
    /// **The spare cells around that extent come on top of the first budget.** Counted in it, they
    /// doubled the cell of one build in six over the shot suite's places, because an exterior is a
    /// few cells high and two more is half as many again. On top, the largest grid there is 88,168
    /// cells, a third over the budget, where the coarser cell had every lookup walk more lamps.
    constexpr std::size_t sMaxCells = 65536;
    constexpr std::size_t sMaxEntries = 262144;

    /// The side a grid starts at, in world units — a quarter of a terrain tile, about half a
    /// lamp's reach, because a cell of side `c` lists every lamp within `reach + c` of it. At one
    /// tile a point in the Guild of Mages weighed twenty-one lamps for the one or two that reached
    /// it, and at a quarter tile it weighs twelve; at an eighth the entries grow eightfold for one
    /// lamp fewer. An exterior overruns the cell budget at this size and doubles back to a tile.
    constexpr double sFirstCell = 256.0;

    using CellBox = Rtx::LightGrid::CellBox;

    /// Where a light stands for the grid, with its reach negated where it takes light away: what
    /// decides its cells and its key, so a light that changed either is binned again.
    osg::Vec4f binnedOn(const Rtx::Light& light)
    {
        const bool takes = light.mIntensity.x() < 0.0f || light.mIntensity.y() < 0.0f || light.mIntensity.z() < 0.0f;
        return osg::Vec4f(light.mPosition, takes ? -light.mReach : light.mReach);
    }

    /// The run a light is binned under: nought for one that lights and one for one that takes
    /// light away.
    std::uint32_t keyOf(const osg::Vec4f& binnedOn)
    {
        return binnedOn.w() < 0.0f ? 1u : 0u;
    }

    /// The block a run of `count` lamps is placed in: a power of two over it, so a run has room for
    /// one lamp more, and none for a run of none.
    std::uint32_t roomFor(std::uint32_t count)
    {
        return count == 0 ? 0u : std::bit_ceil(count + 1u);
    }

    /// Hands `visit` the flat index and the coordinates of every cell in `box`.
    template <typename Visit>
    void forEachCell(const CellBox& box, const osg::Vec3ui& size, Visit visit)
    {
        for (std::uint32_t z = box.mLow.z(); z < box.mHigh.z(); ++z)
            for (std::uint32_t y = box.mLow.y(); y < box.mHigh.y(); ++y)
                for (std::uint32_t x = box.mLow.x(); x < box.mHigh.x(); ++x)
                    visit(static_cast<Rtx::Index>(Rtx::Shaders::lightGridCell(x, y, z, size.x(), size.y())),
                        osg::Vec3ui(x, y, z));
    }

    /// In double, as the bounds are: a lamp's numbers are finite, and in float the distance from
    /// one placed near the end of the range to the grid's origin is not.
    CellBox boxAround(
        const osg::Vec3f& centre, float reach, const osg::Vec3f& origin, float inverseCell, const osg::Vec3ui& size)
    {
        CellBox box;
        for (int axis = 0; axis < 3; ++axis)
        {
            // Clamped rather than rejected: a lamp standing outside the grid still reaches into it,
            // and a cell inside it has to know.
            const double from = double{ centre[axis] } - double{ origin[axis] };
            const double low = (from - double{ reach }) * double{ inverseCell };
            const double high = (from + double{ reach }) * double{ inverseCell };

            const auto span = static_cast<double>(size[axis]);
            box.mLow[axis] = static_cast<std::uint32_t>(std::clamp(std::floor(low), 0.0, span));
            box.mHigh[axis] = static_cast<std::uint32_t>(std::clamp(std::floor(high) + 1.0, 0.0, span));
        }
        return box;
    }
}

namespace Rtx
{
    void LightGrid::rebuild(std::span<const Light> lights)
    {
        mMadeAgain = false;
        mRewritten.clear();

        // A grid nothing has made yet says nothing about where the lamps are, and a world of no
        // lamps cannot be told from one by the lengths alone — both are nought. A build is what
        // gives the grid a cell, so a grid with none was never built. A lamp that came or went
        // renumbers every lamp after it.
        if (mCells.empty() || mBinnedOn.size() != lights.size())
        {
            build(lights);
            return;
        }

        // **Only what moved is binned again, into the grid as it stands.** A lamp carried through
        // a room crosses a cell every few frames and stands in the cells it stood in between, so
        // most frames it moves write nothing; its colour, which the grid never reads, changes on
        // nearly every one. The grid is made again where a lamp's reach leaves it, because a
        // position outside the grid is lit by nothing.
        mMoved.clear();
        for (std::size_t at = 0; at < lights.size(); ++at)
        {
            const Light& light = lights[at];
            const osg::Vec4f stood = binnedOn(light);
            if (mBinnedOn[at] == stood)
                continue;

            if (!covers(light))
            {
                build(lights);
                return;
            }

            const CellBox box = boxAround(light.mPosition, light.mReach, mOrigin, mInverseCell, mSize);
            const std::uint32_t took = keyOf(mBinnedOn[at]);
            const std::uint32_t takes = keyOf(stood);
            if (box != mBoxes[at] || took != takes)
            {
                const auto index = static_cast<std::uint32_t>(at);
                forEachCell(mBoxes[at], mSize, [&](Index cell, const osg::Vec3ui& cellAt) {
                    if (took != takes || !box.contains(cellAt))
                        leave(cell, took, index);
                });
                mMoved.push_back(Moved{ .mIndex = index, .mFrom = mBoxes[at], .mTook = took != 0u });
            }

            mBoxes[at] = box;
            mBinnedOn[at] = stood;
        }

        for (const Moved& moved : mMoved)
        {
            const std::uint32_t takes = keyOf(mBinnedOn[moved.mIndex]);
            const bool rekeyed = moved.mTook != (takes != 0u);
            bool room = true;
            forEachCell(mBoxes[moved.mIndex], mSize, [&](Index cell, const osg::Vec3ui& cellAt) {
                if (room && (rekeyed || !moved.mFrom.contains(cellAt)))
                    room = enter(cell, takes, moved.mIndex);
            });
            if (!room)
            {
                build(lights);
                return;
            }
        }
    }

    bool LightGrid::covers(const Light& light) const
    {
        // In double, for the reason `boxAround` gives.
        const double cell = 1.0 / double{ mInverseCell };
        for (int axis = 0; axis < 3; ++axis)
        {
            const double from = double{ light.mPosition[axis] } - double{ mOrigin[axis] };
            // Asked so a number that is not one answers no, and the grid is made again around it.
            if (!(from - double{ light.mReach } >= 0.0
                    && from + double{ light.mReach } <= static_cast<double>(mSize[axis]) * cell))
                return false;
        }
        return true;
    }

    void LightGrid::build(std::span<const Light> lights)
    {
        mBinnedOn.clear();
        mBinnedOn.reserve(lights.size());
        for (const Light& light : lights)
            mBinnedOn.push_back(binnedOn(light));

        // In double, because the lamps' numbers are finite and their difference in float is not: a
        // lamp placed near the end of the range, which a corrupted record reads as easily as NaN,
        // puts the extent past the largest float.
        osg::BoundingBoxd bounds;
        for (const Light& light : lights)
        {
            const osg::Vec3d reach(light.mReach, light.mReach, light.mReach);
            const osg::Vec3d centre(light.mPosition);
            bounds.expandBy(osg::BoundingBoxd(centre - reach, centre + reach));
        }

        const osg::Vec3d low = bounds.valid() ? bounds._min : osg::Vec3d();
        const osg::Vec3d extent = bounds.valid() ? bounds._max - bounds._min : osg::Vec3d();

        // **A cell to spare on every side of what the lamps reach**, so a lamp carried outward is
        // binned into the grid as it stands: at the exact extent, a lamp walking out past the others
        // made the grid again on every frame it moved, and with a cell to spare it does once a cell.
        // None where there are no lamps, which have no side to move out of.
        const double spare = bounds.valid() ? 1.0 : 0.0;

        // The cell doubles until the grid fits both budgets, or until what the lamps reach fits in a
        // single cell: past that, a lamp is in two cells an axis at most, and more lamps than the
        // entry budget still have to be listed. An axis is capped one past the cell budget before it
        // is cast, because a far lamp gives it more cells than a `uint32_t` holds, and one past
        // fails the test whatever the others hold.
        mBoxes.reserve(lights.size());
        mMoved.reserve(lights.size());
        for (double cell = sFirstCell;; cell *= 2.0)
        {
            mInverseCell = static_cast<float>(1.0 / cell);
            std::size_t reachedCells = 1;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double reached
                    = std::clamp(std::ceil(extent[axis] / cell), 1.0, static_cast<double>(sMaxCells + 1));
                reachedCells *= static_cast<std::size_t>(reached);
                mSize[axis] = static_cast<std::uint32_t>(reached + 2.0 * spare);
                // Held inside the float range, because a lamp near its end reaches past it and a cast
                // from past it is undefined. Such a lamp reaches out of the grid on that side, and is
                // binned into the cells it reaches inside it.
                mOrigin[axis] = static_cast<float>(
                    std::max(low[axis] - spare * cell, double{ std::numeric_limits<float>::lowest() }));
            }

            if (reachedCells > sMaxCells)
                continue;

            // Each lamp's box once, for the budget, the count and the fill alike, and kept for the
            // frames after.
            mBoxes.clear();
            std::size_t entries = 0;
            for (const Light& light : lights)
                entries += mBoxes.emplace_back(boxAround(light.mPosition, light.mReach, mOrigin, mInverseCell, mSize))
                               .getCount();

            if (entries <= sMaxEntries || reachedCells == 1)
                break;
        }

        fill();
    }

    void LightGrid::fill()
    {
        const std::size_t cells = std::size_t{ mSize.x() } * mSize.y() * mSize.z();
        mMadeAgain = true;
        mRewritten.clear();
        mRewritten.reserve(cells);

        mCells.assign(cells, Shaders::GpuLightCell{});
        for (std::size_t index = 0; index < mBoxes.size(); ++index)
        {
            const std::uint32_t key = keyOf(mBinnedOn[index]);
            forEachCell(mBoxes[index], mSize, [&](Index cell, const osg::Vec3ui&) { ++mCells[cell].mCount[key]; });
        }

        // Every run's block, packed in cell order; then room past them for every run to move to a
        // block twice its own once, and for every cell to take a lamp that lights it where it had
        // none, before the list runs out and the grid is made again.
        mRooms.resize(2 * cells);
        mEnd = 0;
        for (std::size_t cell = 0; cell < cells; ++cell)
        {
            Shaders::GpuLightCell& held = mCells[cell];
            for (std::uint32_t key = 0; key < 2; ++key)
            {
                held.mFirst[key] = mEnd;
                mRooms[2 * cell + key] = roomFor(held.mCount[key]);
                mEnd += mRooms[2 * cell + key];
                held.mCount[key] = 0;
            }
        }
        mList.resize(2 * std::size_t{ mEnd } + 2 * cells);

        // In the order the lamps were given, so each run is in that order with no sort.
        for (std::size_t index = 0; index < mBoxes.size(); ++index)
        {
            const std::uint32_t key = keyOf(mBinnedOn[index]);
            forEachCell(mBoxes[index], mSize, [&](Index cell, const osg::Vec3ui&) {
                Shaders::GpuLightCell& held = mCells[cell];
                mList[held.mFirst[key] + held.mCount[key]++] = static_cast<std::uint32_t>(index);
            });
        }
    }

    void LightGrid::leave(Index cell, std::uint32_t key, std::uint32_t index)
    {
        Shaders::GpuLightCell& held = mCells[cell];
        const auto run = mList.begin() + held.mFirst[key];
        const auto end = run + held.mCount[key];

        const auto at = std::lower_bound(run, end, index);
        assert(at != end && *at == index && "a lamp left a cell it was never binned into");
        std::copy(at + 1, end, at);
        --held.mCount[key];
        mRewritten.add(cell);
    }

    bool LightGrid::enter(Index cell, std::uint32_t key, std::uint32_t index)
    {
        Shaders::GpuLightCell& held = mCells[cell];
        std::uint32_t& room = mRooms[2 * std::size_t{ cell } + key];
        if (held.mCount[key] == room)
        {
            const std::uint32_t grown = std::max(2 * room, roomFor(1));
            if (mEnd + grown > mList.size())
                return false;

            std::copy_n(mList.begin() + held.mFirst[key], held.mCount[key], mList.begin() + mEnd);
            held.mFirst[key] = mEnd;
            room = grown;
            mEnd += grown;
        }

        const auto run = mList.begin() + held.mFirst[key];
        const auto end = run + held.mCount[key];

        const auto at = std::upper_bound(run, end, index);
        assert((at == run || at[-1] != index) && "a lamp entered a cell it was already binned into");
        std::copy_backward(at, end, end + 1);
        *at = index;
        ++held.mCount[key];
        mRewritten.add(cell);
        return true;
    }
}
