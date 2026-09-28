#include "lightgrid.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <osg/BoundingBox>
#include <osg/Vec3d>

#include "lightbuilder.hpp"

namespace
{
    /// How many cells the grid may hold, and how many lamp entries across all of them. Two
    /// budgets, because a wide exterior overruns the first and one lamp with an enormous reach
    /// overruns the second, and doubling the cell until both fit recovers either.
    constexpr std::size_t sMaxCells = 65536;
    constexpr std::size_t sMaxEntries = 262144;

    /// The side a grid starts at, in world units — a quarter of a terrain tile, about half a
    /// lamp's reach, because a cell of side `c` lists every lamp within `reach + c` of it. At one
    /// tile a point in the Guild of Mages weighed twenty-one lamps for the one or two that reached
    /// it, and at a quarter tile it weighs twelve; at an eighth the entries grow eightfold for one
    /// lamp fewer. An exterior overruns the cell budget at this size and doubles back to a tile.
    constexpr double sFirstCell = 256.0;

    using CellBox = Rtx::LightGrid::CellBox;

    /// The flat index of a cell, which is the one arithmetic the shader has to agree with.
    std::size_t cellAt(std::uint32_t x, std::uint32_t y, std::uint32_t z, const osg::Vec3ui& size)
    {
        return (std::size_t{ z } * size.y() + y) * size.x() + x;
    }

    /// Hands `visit` the flat index of every cell in `box`.
    template <typename Visit>
    void forEachCell(const CellBox& box, const osg::Vec3ui& size, Visit visit)
    {
        for (std::uint32_t z = box.mLow.z(); z < box.mHigh.z(); ++z)
            for (std::uint32_t y = box.mLow.y(); y < box.mHigh.y(); ++y)
                for (std::uint32_t x = box.mLow.x(); x < box.mHigh.x(); ++x)
                    visit(cellAt(x, y, z, size));
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
        // A list nothing has made yet says nothing about where the lamps are, and a world of no
        // lamps cannot be told from one by the lengths alone — both are nought. `RunList::start` is
        // what puts the first entry in, so an empty list is a grid that was never built. A lamp
        // that came or went renumbers every entry after it.
        if (mList.getWhole().empty() || mBinnedOn.size() != lights.size())
        {
            build(lights);
            return;
        }

        // **Only what moved is binned again, into the grid as it stands.** A lamp carried through
        // a room crosses a cell every few frames and stands in the cells it stood in between, so
        // most frames it moves write nothing; its colour, which the grid never reads, changes on
        // nearly every one. The grid is made again only where a lamp's reach leaves it, because a
        // position outside the grid is lit by nothing.
        bool rebin = false;
        for (std::size_t at = 0; at < lights.size(); ++at)
        {
            const Light& light = lights[at];
            const osg::Vec4f stood(light.mPosition, light.mReach);
            if (mBinnedOn[at] == stood)
                continue;

            if (!covers(light))
            {
                build(lights);
                return;
            }

            const CellBox box = boxAround(light.mPosition, light.mReach, mOrigin, mInverseCell, mSize);
            rebin = rebin || box != mBoxes[at];
            mBoxes[at] = box;
            mBinnedOn[at] = stood;
        }

        if (rebin)
            fill();
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
            mBinnedOn.emplace_back(light.mPosition, light.mReach);

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

        mOrigin = bounds.valid() ? osg::Vec3f(bounds._min) : osg::Vec3f();
        const osg::Vec3d extent = bounds.valid() ? bounds._max - bounds._min : osg::Vec3d();

        // The cell doubles until the grid fits both budgets, or until it is a single cell: past
        // that, doubling drops no entry, and more lamps than the entry budget still have to be
        // listed. An axis is capped one past the cell budget before it is cast, because a far lamp
        // gives it more cells than a `uint32_t` holds, and one past fails the test whatever the
        // others hold.
        mBoxes.reserve(lights.size());
        for (double cell = sFirstCell;; cell *= 2.0)
        {
            mInverseCell = static_cast<float>(1.0 / cell);
            for (int axis = 0; axis < 3; ++axis)
                mSize[axis] = static_cast<std::uint32_t>(
                    std::clamp(std::ceil(extent[axis] / cell), 1.0, static_cast<double>(sMaxCells + 1)));

            const std::size_t cells = std::size_t{ mSize.x() } * mSize.y() * mSize.z();
            if (cells > sMaxCells)
                continue;

            // Each lamp's box once, for the budget, the count and the put alike, and kept for the
            // frames after.
            mBoxes.clear();
            std::size_t entries = 0;
            for (const Light& light : lights)
                entries += mBoxes.emplace_back(boxAround(light.mPosition, light.mReach, mOrigin, mInverseCell, mSize))
                               .getCount();

            if (entries <= sMaxEntries || cells == 1)
                break;
        }

        fill();
    }

    void LightGrid::fill()
    {
        const std::size_t cells = std::size_t{ mSize.x() } * mSize.y() * mSize.z();

        mList.start(cells);
        for (const CellBox& box : mBoxes)
            forEachCell(box, mSize, [&](std::size_t cell) { mList.count(cell); });

        mList.place();
        for (std::size_t index = 0; index < mBoxes.size(); ++index)
            forEachCell(
                mBoxes[index], mSize, [&](std::size_t cell) { mList.put(cell, static_cast<std::uint32_t>(index)); });
    }
}
