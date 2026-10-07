#include "creasesplit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <components/rtx/common/pointhash.hpp>

#include "facecross.hpp"

namespace Rtx
{
    namespace
    {
        /// The angle a triangle has at `corner`, which is what its face weighs in a normal there.
        float cornerAngle(const osg::Vec3f& corner, const osg::Vec3f& next, const osg::Vec3f& last)
        {
            const osg::Vec3f a = next - corner;
            const osg::Vec3f b = last - corner;
            const float lengths = a.length() * b.length();
            if (!(lengths > 0.0f))
                return 0.0f;

            return std::acos(std::clamp((a * b) / lengths, -1.0f, 1.0f));
        }

        /// `face` turned to the side `normal` stands on. What a smoothing group averages is its
        /// faces as its normal sees them, so a face wound against its neighbours is compared and
        /// weighed the right way up.
        osg::Vec3f facing(const osg::Vec3f& face, const osg::Vec3f& normal)
        {
            return face * normal < 0.0f ? -face : face;
        }
    }

    void CreaseSplit::split(const std::span<const osg::Vec3f> positions, const std::span<const osg::Vec3f> normals,
        std::vector<std::uint32_t>& triangles, Split& split)
    {
        split.mNormals.clear();
        split.mSources.clear();

        const std::size_t count = triangles.size() / 3;
        if (count == 0 || normals.size() != positions.size())
            return;

        mFaces.resize(count);
        for (std::size_t t = 0; t < count; ++t)
        {
            mFaces[t] = FaceCross::of(
                positions[triangles[3 * t]], positions[triangles[3 * t + 1]], positions[triangles[3 * t + 2]])
                            .mUnit;
        }

        const std::uint32_t welded = weld(positions);
        mFanStarts.assign(std::size_t{ welded } + 1, 0);
        for (const std::uint32_t vertex : triangles)
            ++mFanStarts[mPositionOf[vertex] + 1];
        for (std::size_t at = 1; at < mFanStarts.size(); ++at)
            mFanStarts[at] += mFanStarts[at - 1];

        mFanCorners.resize(triangles.size());
        mCursor.assign(mFanStarts.begin(), mFanStarts.end() - 1);
        for (std::uint32_t corner = 0; corner < triangles.size(); ++corner)
            mFanCorners[mCursor[mPositionOf[triangles[corner]]]++] = corner;

        // Read off the triangles as they came, while `cutFan` writes the moved corners into them.
        mOriginal.assign(triangles.begin(), triangles.end());

        for (std::uint32_t position = 0; position < welded; ++position)
        {
            const std::uint32_t first = mFanStarts[position];
            const std::uint32_t end = mFanStarts[position + 1];
            if (end - first < 2)
                continue;

            cutFan(std::span(mFanCorners).subspan(first, end - first), positions, normals, triangles, split);
        }
    }

    std::uint32_t CreaseSplit::weld(const std::span<const osg::Vec3f> positions)
    {
        mTable.reset(positions.size());
        mPositionOf.resize(positions.size());

        std::uint32_t welded = 0;
        for (std::uint32_t vertex = 0; vertex < positions.size(); ++vertex)
        {
            const osg::Vec3f& position = positions[vertex];
            for (std::size_t at = mTable.first(hashPoints(std::span(&position, 1)));; at = mTable.next(at))
            {
                const std::uint32_t held = mTable[at];
                if (held == ProbeTable::sEmpty)
                {
                    mTable[at] = vertex;
                    mPositionOf[vertex] = welded++;
                    break;
                }

                if (positions[held] == position)
                {
                    mPositionOf[vertex] = mPositionOf[held];
                    break;
                }
            }
        }

        return welded;
    }

    std::uint32_t CreaseSplit::pieceOf(std::uint32_t corner)
    {
        while (mParent[corner] != corner)
        {
            mParent[corner] = mParent[mParent[corner]];
            corner = mParent[corner];
        }

        return corner;
    }

    void CreaseSplit::cutFan(const std::span<const std::uint32_t> fan, const std::span<const osg::Vec3f> positions,
        const std::span<const osg::Vec3f> normals, std::vector<std::uint32_t>& triangles, Split& split)
    {
        const std::uint32_t size = static_cast<std::uint32_t>(fan.size());
        const auto vertexOf = [&](std::uint32_t at) { return mOriginal[fan[at]]; };
        const auto triangleOf = [&](std::uint32_t at) { return fan[at] / 3; };
        const auto normalOf = [&](std::uint32_t at) -> const osg::Vec3f& { return normals[vertexOf(at)]; };

        // The corner at `ahead` places after `at` in its triangle, as the file wound it.
        const auto neighbour = [&](std::uint32_t at, std::uint32_t ahead) {
            const std::uint32_t corner = fan[at];
            return mOriginal[corner - corner % 3 + (corner + ahead) % 3];
        };

        // Every corner's group is named by its first corner, and every corner starts a piece of its
        // own. A corner with no normal belongs to no group, and is left as it is. Sorted by normal
        // and then by place, so a group is a run whose first corner names it — a pole's or a
        // degenerate fan's hundreds of corners in a sort, not in a search each.
        mGroup.resize(size);
        mParent.resize(size);
        mGroupState.assign(size, GroupState::Smooth);
        mOrder.clear();
        for (std::uint32_t at = 0; at < size; ++at)
        {
            mParent[at] = at;
            mGroup[at] = sNoEntry;
            if (normalOf(at).length2() > 0.0f)
                mOrder.push_back(at);
        }
        std::sort(mOrder.begin(), mOrder.end(), [&](std::uint32_t a, std::uint32_t b) {
            return normalOf(a) != normalOf(b) ? normalOf(a) < normalOf(b) : a < b;
        });
        for (std::size_t at = 0; at < mOrder.size(); ++at)
            mGroup[mOrder[at]]
                = at > 0 && normalOf(mOrder[at]) == normalOf(mOrder[at - 1]) ? mGroup[mOrder[at - 1]] : mOrder[at];

        // Two corners of one group across an edge of the fan: one piece where the edge is smooth, and
        // the group marked where it is hard. Corners meet across an edge where they share a
        // neighbouring position, so each corner is keyed by its group and each of its two
        // neighbours, and only corners of one key are compared.
        mKeyed.clear();
        for (std::uint32_t at = 0; at < size; ++at)
            if (mGroup[at] != sNoEntry)
                for (const std::uint32_t ahead : { 1u, 2u })
                    mKeyed.push_back(
                        Keyed{ .mFirst = mGroup[at], .mSecond = mPositionOf[neighbour(at, ahead)], .mAt = at });
        std::sort(mKeyed.begin(), mKeyed.end());

        bool anyHard = false;
        for (std::size_t from = 0; from < mKeyed.size();)
        {
            std::size_t to = from + 1;
            while (to < mKeyed.size() && mKeyed[to].mFirst == mKeyed[from].mFirst
                && mKeyed[to].mSecond == mKeyed[from].mSecond)
                ++to;

            for (std::size_t one = from; one < to; ++one)
                for (std::size_t other = one + 1; other < to; ++other)
                {
                    const std::uint32_t i = mKeyed[one].mAt;
                    const std::uint32_t j = mKeyed[other].mAt;
                    if (triangleOf(j) == triangleOf(i))
                        continue;

                    const osg::Vec3f a = facing(mFaces[triangleOf(i)], normalOf(i));
                    const osg::Vec3f b = facing(mFaces[triangleOf(j)], normalOf(j));
                    if (a.length2() > 0.0f && b.length2() > 0.0f && a * b < sHardCosine)
                    {
                        mGroupState[mGroup[i]] = GroupState::Hard;
                        anyHard = true;
                    }
                    else
                        mParent[pieceOf(i)] = pieceOf(j);
                }

            from = to;
        }

        if (!anyHard)
            return;

        // A hard edge cuts its group only where the group's smooth edges do not join it back into
        // one piece around the edge.
        for (std::uint32_t at = 0; at < size; ++at)
            if (mGroup[at] != sNoEntry && mGroupState[mGroup[at]] != GroupState::Smooth
                && pieceOf(at) != pieceOf(mGroup[at]))
                mGroupState[mGroup[at]] = GroupState::Cut;

        const auto cut
            = [&](std::uint32_t at) { return mGroup[at] != sNoEntry && mGroupState[mGroup[at]] == GroupState::Cut; };

        mPieceNormal.assign(size, osg::Vec3f());
        for (std::uint32_t at = 0; at < size; ++at)
            if (cut(at))
                mPieceNormal[pieceOf(at)] += facing(mFaces[triangleOf(at)], normalOf(at))
                    * cornerAngle(positions[vertexOf(at)], positions[neighbour(at, 1)], positions[neighbour(at, 2)]);

        // Each cut corner's first corner of the same vertex and piece, which places the vertex every
        // later one moves to, and its first corner of the same vertex, whose piece keeps the vertex.
        mKeyed.clear();
        for (std::uint32_t at = 0; at < size; ++at)
            if (cut(at))
                mKeyed.push_back(Keyed{ .mFirst = vertexOf(at), .mSecond = pieceOf(at), .mAt = at });
        std::sort(mKeyed.begin(), mKeyed.end());

        mPlacedBy.resize(size);
        mVertexFirst.resize(size);
        for (std::size_t from = 0; from < mKeyed.size();)
        {
            std::size_t to = from;
            std::uint32_t first = mKeyed[from].mAt;
            while (to < mKeyed.size() && mKeyed[to].mFirst == mKeyed[from].mFirst)
                first = std::min(first, mKeyed[to++].mAt);

            for (std::size_t at = from; at < to; ++at)
            {
                const bool newPiece = at == from || mKeyed[at].mSecond != mKeyed[at - 1].mSecond;
                mPlacedBy[mKeyed[at].mAt] = newPiece ? mKeyed[at].mAt : mPlacedBy[mKeyed[at - 1].mAt];
                mVertexFirst[mKeyed[at].mAt] = first;
            }
            from = to;
        }

        mPlacedAt.resize(size);
        for (std::uint32_t at = 0; at < size; ++at)
        {
            if (!cut(at))
                continue;

            // The first cut takes its copy of the input's normals, which every cut after writes over
            // and adds to: nothing is copied for a mesh with no hard edge in a group.
            if (split.mNormals.empty())
                split.mNormals.assign(normals.begin(), normals.end());

            if (mPlacedBy[at] != at)
            {
                triangles[fan[at]] = mPlacedAt[mPlacedBy[at]];
                continue;
            }

            const osg::Vec3f summed = mPieceNormal[pieceOf(at)];
            const osg::Vec3f normal = summed.length2() > 0.0f ? summed / summed.length() : normalOf(at);

            // The vertex's first piece keeps the vertex, and every other one is a copy of it.
            const std::uint32_t vertex = vertexOf(at);
            std::uint32_t placedAt = vertex;
            if (mVertexFirst[at] != at)
            {
                placedAt = static_cast<std::uint32_t>(split.mNormals.size());
                split.mSources.push_back(vertex);
                split.mNormals.push_back(normal);
            }
            else
                split.mNormals[vertex] = normal;

            mPlacedAt[at] = placedAt;
            triangles[fan[at]] = placedAt;
        }
    }
}
