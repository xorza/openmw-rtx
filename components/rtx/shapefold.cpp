#include "shapefold.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>

#include <osg/BoundingBox>
#include <osg/Vec3f>

namespace Rtx
{
    namespace
    {
        bool before(const osg::Vec3f& a, const osg::Vec3f& b)
        {
            if (a.x() != b.x())
                return a.x() < b.x();
            if (a.y() != b.y())
                return a.y() < b.y();
            return a.z() < b.z();
        }

        /// The bits of a run of points, mixed to a hash: a triangle's three corners, or an edge's
        /// two ends. Mixed here rather than through `Misc::hashCombine`, which reaches
        /// `std::hash<float>` — a byte-wise murmur over four bytes — and eighteen of those per
        /// triangle cost more than the fold around them, where FNV over the bits and one final mix
        /// cost nine multiplies. A zero is normalised first, because -0 and 0 compare equal in a
        /// spelling and in an edge, and a hash that told the two apart would never find the twin.
        std::size_t hashPoints(std::span<const osg::Vec3f> points)
        {
            std::uint64_t seed = 0xcbf29ce484222325ull;
            for (const osg::Vec3f& point : points)
                for (const float value : { point.x(), point.y(), point.z() })
                {
                    seed ^= std::bit_cast<std::uint32_t>(value == 0.0f ? 0.0f : value);
                    seed *= 0x100000001b3ull;
                }

            // FNV moves its high bits far more than its low ones, and the tables mask the low ones.
            seed ^= seed >> 29;
            seed *= 0xbf58476d1ce4e5b9ull;
            seed ^= seed >> 32;

            return static_cast<std::size_t>(seed);
        }

        /// How far apart a pocket's two walls stand at most, in the mesh's own units. **The ship's
        /// sail is what set it**: along each face's normal its sheets part by no more than this over
        /// ninety-nine hundredths of its area, and a quarter of it at four. A wall of cloth or
        /// plaster seen from its two sides is that thin; a gap wider than this is the shape.
        constexpr float sPocketReach = 8.0f;

        /// And no more than this share of the shape's diagonal, so a small thing keeps its form: a
        /// tankard twenty units tall whose inner wall stood eight inside the outer one is a vessel and
        /// not a pocket. Across the game's meshes the vessels, helmets and heads the reach alone took
        /// for pockets all fall under this.
        constexpr float sPocketShare = 0.05f;

        /// How far two walls may lean apart and still face each other: a hundred and twenty degrees,
        /// the cosine of which is this. A crease in the cloth leans them; a wall at right angles to
        /// the ray is not across from it.
        constexpr float sFacing = -0.5f;

        /// The winding number at a gap's middle below which the gap is inside out. **Minus one is a
        /// pocket and nought is a slot**, and the halves between are what open sheets add near their
        /// edges: an unbacked card contributes a half on its face, so the cut sits between minus one
        /// and minus a half.
        constexpr double sInsideOut = -0.75;

        /// Where on a triangle its rays leave from, as the weights of its corners: the centroid and a
        /// point toward each corner, so a triangle half across a pocket's edge is seen on both sides
        /// of it.
        constexpr std::array<std::array<float, 3>, 4> sPocketSamples{ {
            { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f },
            { 0.5f, 0.25f, 0.25f },
            { 0.25f, 0.5f, 0.25f },
            { 0.25f, 0.25f, 0.5f },
        } };

        /// How many of its samples have to open on a pocket for a triangle to be one of its walls, its
        /// centroid first among them: half, so a triangle that only grazes one stays.
        constexpr std::uint8_t sWallSamples = 2;

        /// Keeps the triangles of `indices` that `dropped` does not name, in their order, compacting
        /// it in place.
        template <class Dropped>
        void compactTriangles(std::vector<std::uint32_t>& indices, Dropped&& dropped)
        {
            const std::size_t count = indices.size() / 3;
            std::size_t kept = 0;
            for (std::size_t t = 0; t < count; ++t)
            {
                if (dropped(t))
                    continue;
                if (kept != t)
                    std::copy_n(indices.begin() + static_cast<std::ptrdiff_t>(3 * t), 3,
                        indices.begin() + static_cast<std::ptrdiff_t>(3 * kept));
                ++kept;
            }
            indices.resize(kept * 3);
        }
    }

    bool ShapeFold::Corners::operator==(const Corners& other) const
    {
        return mCorner[0] == other.mCorner[0] && mCorner[1] == other.mCorner[1] && mCorner[2] == other.mCorner[2];
    }

    std::size_t ShapeFold::Corners::hash() const
    {
        return hashPoints(mCorner);
    }

    ShapeFold::Corners ShapeFold::canonical(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c)
    {
        const osg::Vec3f* corners[3] = { &a, &b, &c };
        int least = 0;
        for (int i = 1; i < 3; ++i)
            if (before(*corners[i], *corners[least]))
                least = i;

        return Corners{ { *corners[least], *corners[(least + 1) % 3], *corners[(least + 2) % 3] } };
    }

    bool ShapeFold::closes(std::span<const osg::Vec3f> positions, std::span<const std::uint32_t> indices)
    {
        const std::size_t count = indices.size() / 3;

        // A closed shape carries three halves of a triangle's worth of edges, because every edge
        // of one has a triangle each way. Two things follow and neither is a threshold: an odd
        // triangle count cannot close, and no shape passes `mostEdges` distinct edges and still
        // closes. Nothing closes nothing, which is the third.
        //
        // They are here for the merged terrain chunk, which is hundreds of statics whose first
        // grass card already passes the limit: a saving in the fold's tail alone.
        if (count == 0 || count % 2 != 0)
            return false;

        const std::size_t mostEdges = count * 3 / 2;

        // Sized against what can be reached and not against what could be pushed. The limit
        // above bounds the table, so the same one entry in two costs half the slots a bound of
        // every side of every triangle would — half a megabyte of the `assign` below, on a chunk of
        // thirty thousand triangles.
        const std::size_t slots = std::bit_ceil(std::max<std::size_t>(mostEdges * 2, 16));
        const std::size_t mask = slots - 1;

        mEdgeTable.assign(slots, sNoEntry);
        mEdges.clear();
        mEdges.reserve(mostEdges);

        for (std::size_t t = 0; t < count; ++t)
            for (int side = 0; side < 3; ++side)
            {
                const osg::Vec3f& from = positions[indices[3 * t + side]];
                const osg::Vec3f& to = positions[indices[3 * t + (side + 1) % 3]];

                // A degenerate edge belongs to no pair and would pair with itself.
                if (from == to)
                    return false;

                const bool forward = before(from, to);
                const osg::Vec3f& low = forward ? from : to;
                const osg::Vec3f& high = forward ? to : from;

                const osg::Vec3f ends[2] = { low, high };
                std::size_t at = hashPoints(ends) & mask;
                for (;; at = (at + 1) & mask)
                {
                    const std::uint32_t held = mEdgeTable[at];
                    if (held == sNoEntry)
                    {
                        // Reaching the limit is what a closed shape does, and passing it is what
                        // says this is not one. A tetrahedron is four triangles and six edges,
                        // which is exactly the limit.
                        if (mEdges.size() == mostEdges)
                            return false;

                        mEdgeTable[at] = static_cast<std::uint32_t>(mEdges.size());
                        mEdges.push_back(Edge{ { low, high }, 0, 0 });
                        break;
                    }

                    if (mEdges[held].mEnd[0] == low && mEdges[held].mEnd[1] == high)
                        break;
                }

                Edge& edge = mEdges[mEdgeTable[at]];
                (forward ? edge.mForward : edge.mBackward) += 1;

                // A third triangle on an edge is a shape no side can be taken against, and there is
                // no answer further on that could put it right.
                if (edge.mForward > 1 || edge.mBackward > 1)
                    return false;
            }

        for (const Edge& edge : mEdges)
            if (edge.mForward != 1 || edge.mBackward != 1)
                return false;

        return true;
    }

    bool ShapeFold::dropPockets(std::span<const osg::Vec3f> positions, std::vector<std::uint32_t>& indices)
    {
        const std::size_t count = indices.size() / 3;
        if (count < 2)
            return false;

        mTree.build(positions, indices);
        const osg::BoundingBoxf& bounds = mTree.getBounds();
        if (!bounds.valid())
            return false;

        const float diagonal = (bounds._max - bounds._min).length();
        const float reach = std::min(sPocketReach, sPocketShare * diagonal);
        if (!(reach > 0.0f))
            return false;

        // A ray this short meets its own triangle's neighbours in the rounding and nothing else.
        const float nearest = 1.0e-5f * diagonal;

        const auto sampleOf = [&](std::size_t t, const std::array<float, 3>& weights) {
            return positions[indices[3 * t]] * weights[0] + positions[indices[3 * t + 1]] * weights[1]
                + positions[indices[3 * t + 2]] * weights[2];
        };

        mPocketSamples.assign(count, 0);
        mAcrossStarts.assign(count + 1, 0);
        mAcross.clear();
        for (std::uint32_t t = 0; t < count; ++t)
        {
            const osg::Vec3f& normal = mTree.getNormal(t);

            // **The centroid first, and the rest only where it opened on a pocket**, since a wall is
            // a triangle whose centroid opens on one and at least one more sample with it — most
            // triangles meet nothing within the reach and cost one ray.
            const osg::Vec3f centroid = sampleOf(t, sPocketSamples[0]);
            const PocketTree::Facing wall = normal != osg::Vec3f()
                ? mTree.firstFacing(t, centroid, nearest, reach, sFacing)
                : PocketTree::Facing{ std::numeric_limits<float>::infinity(), 0 };
            if (wall.mDistance <= reach && mTree.windingAt(centroid + normal * (0.5f * wall.mDistance)) < sInsideOut)
            {
                mPocketSamples[t] = 1;
                mAcross.push_back(wall.mTriangle);

                for (std::size_t sample = 1; sample < sPocketSamples.size(); ++sample)
                {
                    const osg::Vec3f from = sampleOf(t, sPocketSamples[sample]);
                    const PocketTree::Facing met = mTree.firstFacing(t, from, nearest, reach, sFacing);
                    if (met.mDistance > reach)
                        continue;

                    // **The same two faces bound the same gap**, and a winding number holds over a
                    // region no surface crosses: a surface across this one's would have been met
                    // first. So a sample that meets the centroid's wall is in its pocket, and only
                    // one that meets another is asked again.
                    if (met.mTriangle == wall.mTriangle
                        || mTree.windingAt(from + normal * (0.5f * met.mDistance)) < sInsideOut)
                    {
                        ++mPocketSamples[t];
                        mAcross.push_back(met.mTriangle);
                    }
                }
            }
            mAcrossStarts[t + 1] = static_cast<std::uint32_t>(mAcross.size());
        }

        // In file order, so of two walls across from each other the one the file wrote first is
        // the one that stays, as a twin's does.
        mPocketDropped.assign(count, 0);
        bool dropped = false;
        for (std::size_t t = 0; t < count; ++t)
        {
            if (mPocketSamples[t] < sWallSamples)
                continue;
            for (std::uint32_t at = mAcrossStarts[t]; at < mAcrossStarts[t + 1]; ++at)
            {
                const std::uint32_t other = mAcross[at];
                if (other < t && mPocketSamples[other] >= sWallSamples && mPocketDropped[other] == 0)
                {
                    mPocketDropped[t] = 1;
                    dropped = true;
                    break;
                }
            }
        }

        if (dropped)
            compactTriangles(indices, [&](std::size_t t) { return mPocketDropped[t] != 0; });
        return dropped;
    }

    FoldedShape ShapeFold::fold(std::span<const osg::Vec3f> positions, std::vector<std::uint32_t>& indices)
    {
        const std::size_t count = indices.size() / 3;
        if (count == 0)
            return FoldedShape{};

        // Grown to the largest mesh yet folded and never shrunk, so a run of them stops resizing:
        // every entry below `count` is written before it is read.
        if (mSpelling.size() < count)
        {
            mSpelling.resize(count);
            mHashes.resize(count);
        }

        for (std::size_t t = 0; t < count; ++t)
        {
            mSpelling[t]
                = canonical(positions[indices[3 * t]], positions[indices[3 * t + 1]], positions[indices[3 * t + 2]]);
            mHashes[t] = mSpelling[t].hash();
        }

        const std::size_t slots = std::bit_ceil(std::max<std::size_t>(count * 2, 16));
        const std::size_t mask = slots - 1;
        mTable.assign(slots, sNoEntry);
        mNext.assign(count, sNoEntry);

        // From the last triangle back, so each chain runs forwards. The pairing below takes the
        // first triangle of a spelling that is still unpaired, and which one that is decides which
        // copy of a doubled card survives: the one the file wrote first, as it was before anything
        // was folded.
        for (std::size_t t = count; t-- > 0;)
        {
            std::size_t at = mHashes[t] & mask;
            for (;; at = (at + 1) & mask)
            {
                const std::uint32_t head = mTable[at];
                if (head == sNoEntry)
                    break;

                if (mHashes[head] == mHashes[t] && mSpelling[head] == mSpelling[t])
                {
                    mNext[t] = head;
                    break;
                }
            }

            mTable[at] = static_cast<std::uint32_t>(t);
        }

        mFates.assign(count, Fate::Alone);
        for (std::size_t t = 0; t < count; ++t)
        {
            if (mFates[t] != Fate::Alone)
                continue;

            const Corners reversed
                = canonical(positions[indices[3 * t]], positions[indices[3 * t + 2]], positions[indices[3 * t + 1]]);
            const std::size_t hash = reversed.hash();

            std::uint32_t head = sNoEntry;
            for (std::size_t at = hash & mask;; at = (at + 1) & mask)
            {
                const std::uint32_t candidate = mTable[at];
                if (candidate == sNoEntry)
                    break;

                if (mHashes[candidate] == hash && mSpelling[candidate] == reversed)
                {
                    head = candidate;
                    break;
                }
            }

            for (std::uint32_t other = head; other != sNoEntry; other = mNext[other])
            {
                // Itself, for a degenerate triangle whose reverse is its own spelling.
                if (other == t || mFates[other] != Fate::Alone)
                    continue;

                mFates[t] = Fate::Kept;
                mFates[other] = Fate::Dropped;
                break;
            }
        }

        bool sheet = true;
        bool folded = false;
        for (const Fate fate : mFates)
        {
            sheet = sheet && fate != Fate::Alone;
            folded = folded || fate != Fate::Alone;
        }
        compactTriangles(indices, [&](std::size_t t) { return mFates[t] == Fate::Dropped; });

        // After the twins, so a doubled card's copy is never taken for a pocket's wall: it stands
        // at no distance at all, where a pocket's stands a few units off.
        const bool pocketed = dropPockets(positions, indices);

        // On what survives, because that is what a ray will meet. A doubled card folds to one
        // quad, which has a boundary; a shape with no twins folds to itself and is whatever it was.
        return FoldedShape{
            .mSheet = sheet, .mFolded = folded, .mPocketed = pocketed, .mClosed = closes(positions, indices)
        };
    }
}
