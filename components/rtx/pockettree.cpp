#include "pockettree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

#include <osg/Vec3d>

namespace Rtx
{
    namespace
    {
        /// The most triangles a leaf holds.
        constexpr std::uint32_t sLeafTriangles = 8;

        /// How many of its own radii off a node has to be for its dipole to stand for its triangles
        /// in a winding number: Barill et al.'s accuracy parameter at the value their paper runs
        /// with. Its error is a few thousandths, and the question `ShapeFold` asks of it tells minus
        /// one from nought and minus a half.
        constexpr float sFarRadii = 2.0f;

        /// The box of a tree over no triangle.
        const osg::BoundingBoxf sNoBounds;

        /// The solid angle the triangle `a`, `b`, `c` subtends from the origin, signed by its
        /// winding (Van Oosterom and Strackee, 1983): the exact term a winding number sums.
        double solidAngle(const osg::Vec3d& a, const osg::Vec3d& b, const osg::Vec3d& c)
        {
            const double la = a.length();
            const double lb = b.length();
            const double lc = c.length();
            const double det = a * (b ^ c);
            const double div = la * lb * lc + (a * b) * lc + (b * c) * la + (c * a) * lb;
            return 2.0 * std::atan2(det, div);
        }

        /// Where along `direction` from `origin` the ray meets the triangle `a`, `b`, `c`, from
        /// either side, or infinity where it misses (Moller and Trumbore, 1997).
        float rayMeets(const osg::Vec3f& origin, const osg::Vec3f& direction, const osg::Vec3f& a, const osg::Vec3f& b,
            const osg::Vec3f& c)
        {
            constexpr float miss = std::numeric_limits<float>::infinity();

            const osg::Vec3f edge1 = b - a;
            const osg::Vec3f edge2 = c - a;
            const osg::Vec3f p = direction ^ edge2;
            const float det = edge1 * p;
            if (std::abs(det) <= std::numeric_limits<float>::min())
                return miss;

            const float inverse = 1.0f / det;
            const osg::Vec3f s = origin - a;
            const float u = (s * p) * inverse;
            if (u < 0.0f || u > 1.0f)
                return miss;

            const osg::Vec3f q = s ^ edge1;
            const float v = (direction * q) * inverse;
            if (v < 0.0f || u + v > 1.0f)
                return miss;

            return (edge2 * q) * inverse;
        }

        /// Whether the segment from `from` along `direction` for `length` touches `box`.
        bool segmentTouches(
            const osg::Vec3f& from, const osg::Vec3f& direction, float length, const osg::BoundingBoxf& box)
        {
            float enter = 0.0f;
            float leave = length;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (direction[axis] == 0.0f)
                {
                    if (from[axis] < box._min[axis] || from[axis] > box._max[axis])
                        return false;
                    continue;
                }

                const float inverse = 1.0f / direction[axis];
                float near = (box._min[axis] - from[axis]) * inverse;
                float far = (box._max[axis] - from[axis]) * inverse;
                if (near > far)
                    std::swap(near, far);
                enter = std::max(enter, near);
                leave = std::min(leave, far);
                if (enter > leave)
                    return false;
            }
            return true;
        }
    }

    void PocketTree::build(std::span<const osg::Vec3f> positions, std::span<const std::uint32_t> indices)
    {
        mPositions = positions;
        mIndices = indices;

        const std::size_t count = indices.size() / 3;
        mNormals.resize(count);
        mAreas.resize(count);
        mCentroids.resize(count);
        mOrder.clear();
        for (std::uint32_t t = 0; t < count; ++t)
        {
            const osg::Vec3f cross = (corner(t, 1) - corner(t, 0)) ^ (corner(t, 2) - corner(t, 0));
            const float twiceArea = cross.length();
            mNormals[t] = twiceArea > 0.0f ? cross / twiceArea : osg::Vec3f();
            mAreas[t] = 0.5f * twiceArea;
            mCentroids[t] = (corner(t, 0) + corner(t, 1) + corner(t, 2)) / 3.0f;
            if (twiceArea > 0.0f)
                mOrder.push_back(t);
        }

        mNodes.clear();
        if (mOrder.empty())
            return;

        // Split at the median centroid along the longest side, one node's two children made
        // together so the second is always the first plus one. Walked with a stack of
        // (node, begin, end) rather than by recursion.
        mNodes.emplace_back();
        mStack.clear();
        mStack.insert(mStack.end(), { 0u, 0u, static_cast<std::uint32_t>(mOrder.size()) });
        while (!mStack.empty())
        {
            const std::uint32_t end = mStack.back();
            mStack.pop_back();
            const std::uint32_t begin = mStack.back();
            mStack.pop_back();
            const std::uint32_t node = mStack.back();
            mStack.pop_back();

            Node built;
            osg::BoundingBoxf centroids;
            osg::Vec3f weighted;
            float area = 0.0f;
            for (std::uint32_t at = begin; at < end; ++at)
            {
                const std::uint32_t t = mOrder[at];
                for (int which = 0; which < 3; ++which)
                    built.mBox.expandBy(corner(t, which));
                centroids.expandBy(mCentroids[t]);
                weighted += mCentroids[t] * mAreas[t];
                area += mAreas[t];
                built.mDipole += mNormals[t] * mAreas[t];
            }
            built.mCentre = weighted / area;
            for (std::uint32_t at = begin; at < end; ++at)
                for (int which = 0; which < 3; ++which)
                    built.mRadius = std::max(built.mRadius, (corner(mOrder[at], which) - built.mCentre).length());

            if (end - begin <= sLeafTriangles)
            {
                built.mFirst = begin;
                built.mCount = end - begin;
                mNodes[node] = built;
                continue;
            }

            const osg::Vec3f spread = centroids._max - centroids._min;
            const int axis
                = spread.x() >= spread.y() && spread.x() >= spread.z() ? 0 : (spread.y() >= spread.z() ? 1 : 2);
            const std::uint32_t middle = begin + (end - begin) / 2;
            std::nth_element(mOrder.begin() + begin, mOrder.begin() + middle, mOrder.begin() + end,
                [&](std::uint32_t a, std::uint32_t b) { return mCentroids[a][axis] < mCentroids[b][axis]; });

            built.mFirst = static_cast<std::uint32_t>(mNodes.size());
            mNodes[node] = built;
            mNodes.emplace_back();
            mNodes.emplace_back();
            mStack.insert(mStack.end(), { built.mFirst, begin, middle, built.mFirst + 1, middle, end });
        }
    }

    const osg::BoundingBoxf& PocketTree::getBounds() const
    {
        return mNodes.empty() ? sNoBounds : mNodes.front().mBox;
    }

    PocketTree::Facing PocketTree::firstFacing(
        std::uint32_t t, const osg::Vec3f& from, float nearest, float reach, float facing)
    {
        const osg::Vec3f& normal = mNormals[t];
        Facing met{ std::numeric_limits<float>::infinity(), 0 };
        if (mNodes.empty())
            return met;

        mStack.assign(1, 0u);
        while (!mStack.empty())
        {
            const Node& node = mNodes[mStack.back()];
            mStack.pop_back();
            if (!segmentTouches(from, normal, std::min(met.mDistance, reach), node.mBox))
                continue;

            if (node.mCount == 0)
            {
                mStack.insert(mStack.end(), { node.mFirst, node.mFirst + 1 });
                continue;
            }

            for (std::uint32_t at = node.mFirst; at < node.mFirst + node.mCount; ++at)
            {
                const std::uint32_t other = mOrder[at];
                if (other == t || mNormals[other] * normal >= facing)
                    continue;
                const float distance = rayMeets(from, normal, corner(other, 0), corner(other, 1), corner(other, 2));
                if (distance > nearest && distance <= reach && distance < met.mDistance)
                    met = Facing{ distance, other };
            }
        }
        return met;
    }

    double PocketTree::windingAt(const osg::Vec3f& point)
    {
        const osg::Vec3d origin(point);
        double sum = 0.0;
        if (mNodes.empty())
            return sum;

        mStack.assign(1, 0u);
        while (!mStack.empty())
        {
            const Node& node = mNodes[mStack.back()];
            mStack.pop_back();

            const osg::Vec3d toward = osg::Vec3d(node.mCentre) - origin;
            const double distance = toward.length();
            if (distance > static_cast<double>(sFarRadii * node.mRadius))
            {
                sum += (osg::Vec3d(node.mDipole) * toward) / (distance * distance * distance);
                continue;
            }

            if (node.mCount == 0)
            {
                mStack.insert(mStack.end(), { node.mFirst, node.mFirst + 1 });
                continue;
            }

            for (std::uint32_t at = node.mFirst; at < node.mFirst + node.mCount; ++at)
            {
                const std::uint32_t t = mOrder[at];
                sum += solidAngle(osg::Vec3d(corner(t, 0)) - origin, osg::Vec3d(corner(t, 1)) - origin,
                    osg::Vec3d(corner(t, 2)) - origin);
            }
        }
        return sum / (4.0 * std::numbers::pi);
    }
}
