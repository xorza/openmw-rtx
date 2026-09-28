#include "shapepass.hpp"

#include <cassert>
#include <functional>

#include "contentkey.hpp"

namespace Rtx
{
    void ShapePass::digest(const Input& input, ContentDigest& digest) const
    {
        digest.add(input.mPositions);
        digest.add(input.mNormals);
        digest.add(input.mTriangles);
        digest.addValue(input.mSplits);
    }

    void ShapePass::run(const Input& input, Output& output)
    {
        // An input spanning its own output would be read as it is overwritten.
        [[maybe_unused]] const std::uint32_t* const from = input.mTriangles.data();
        [[maybe_unused]] const std::uint32_t* const kept = output.mKept.data();
        assert((std::less_equal<>()(from + input.mTriangles.size(), kept)
                   || std::less_equal<>()(kept + output.mKept.capacity(), from))
            && "a shape's input spans its own output");

        output.mKept.assign(input.mTriangles.begin(), input.mTriangles.end());
        output.mShape = mFold.fold(input.mPositions, output.mKept);

        // **A sheet keeps the normals it was written with.** Nothing a split is for reaches one: it
        // takes a light's side from its plane and its shadow ray is not lifted (`Surface::mSheet`).
        // And foliage averages its cards' normals on purpose, so a clump lights as one volume — cut
        // at the angles its cards cross at, a canopy would light card by card. No normals is nothing
        // to split either, which the split answers by writing nothing.
        const bool splits = input.mSplits && !output.mShape.mSheet;
        CreaseSplit::Split split{ .mNormals = output.mNormals, .mSources = output.mSources };
        mSplit.split(input.mPositions, splits ? input.mNormals : std::span<const osg::Vec3f>(), output.mKept, split);
    }
}
