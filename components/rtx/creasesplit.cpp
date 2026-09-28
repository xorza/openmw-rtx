#include "creasesplit.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "pointhash.hpp"

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
            const osg::Vec3f& a = positions[triangles[3 * t]];
            osg::Vec3f face = (positions[triangles[3 * t + 1]] - a) ^ (positions[triangles[3 * t + 2]] - a);
            const float length = face.length();
            mFaces[t] = length > 0.0f ? face / length : osg::Vec3f();
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
        const std::size_t slots = std::bit_ceil(std::max<std::size_t>(positions.size() * 2, 16));
        const std::size_t mask = slots - 1;
        mTable.assign(slots, sNoEntry);
        mPositionOf.resize(positions.size());

        std::uint32_t welded = 0;
        for (std::uint32_t vertex = 0; vertex < positions.size(); ++vertex)
        {
            const osg::Vec3f& position = positions[vertex];
            for (std::size_t at = hashPoints(std::span(&position, 1)) & mask;; at = (at + 1) & mask)
            {
                const std::uint32_t held = mTable[at];
                if (held == sNoEntry)
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
        // own. A corner with no normal belongs to no group, and is left as it is.
        mGroup.resize(size);
        mParent.resize(size);
        mGroupState.assign(size, GroupState::Smooth);
        for (std::uint32_t at = 0; at < size; ++at)
        {
            mParent[at] = at;
            mGroup[at] = sNoEntry;
            if (!(normalOf(at).length2() > 0.0f))
                continue;

            std::uint32_t first = 0;
            while (normalOf(first) != normalOf(at))
                ++first;
            mGroup[at] = first;
        }

        // Two corners of one group across an edge of the fan: one piece where the edge is smooth, and
        // the group marked where it is hard.
        bool anyHard = false;
        for (std::uint32_t i = 0; i < size; ++i)
        {
            if (mGroup[i] == sNoEntry)
                continue;

            const std::uint32_t mine[2] = { mPositionOf[neighbour(i, 1)], mPositionOf[neighbour(i, 2)] };
            for (std::uint32_t j = i + 1; j < size; ++j)
            {
                if (mGroup[j] != mGroup[i] || triangleOf(j) == triangleOf(i))
                    continue;

                const std::uint32_t theirs[2] = { mPositionOf[neighbour(j, 1)], mPositionOf[neighbour(j, 2)] };
                const bool adjacent
                    = mine[0] == theirs[0] || mine[0] == theirs[1] || mine[1] == theirs[0] || mine[1] == theirs[1];
                if (!adjacent)
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

        mPlaced.clear();
        for (std::uint32_t at = 0; at < size; ++at)
        {
            if (!cut(at))
                continue;

            // The first cut takes its copy of the input's normals, which every cut after writes over
            // and adds to: nothing is copied for a mesh with no hard edge in a group.
            if (split.mNormals.empty())
                split.mNormals.assign(normals.begin(), normals.end());

            const std::uint32_t vertex = vertexOf(at);
            const std::uint32_t piece = pieceOf(at);
            const auto held = std::find_if(mPlaced.begin(), mPlaced.end(),
                [&](const Placed& placed) { return placed.mVertex == vertex && placed.mPiece == piece; });
            if (held != mPlaced.end())
            {
                triangles[fan[at]] = held->mPlacedAt;
                continue;
            }

            const osg::Vec3f summed = mPieceNormal[piece];
            const osg::Vec3f normal = summed.length2() > 0.0f ? summed / summed.length() : normalOf(at);

            // The vertex's first piece keeps the vertex, and every other one is a copy of it.
            const bool met = std::any_of(
                mPlaced.begin(), mPlaced.end(), [&](const Placed& placed) { return placed.mVertex == vertex; });
            std::uint32_t placedAt = vertex;
            if (met)
            {
                placedAt = static_cast<std::uint32_t>(split.mNormals.size());
                split.mSources.push_back(vertex);
                split.mNormals.push_back(normal);
            }
            else
                split.mNormals[vertex] = normal;

            mPlaced.push_back(Placed{ .mVertex = vertex, .mPiece = piece, .mPlacedAt = placedAt });
            triangles[fan[at]] = placedAt;
        }
    }
}
