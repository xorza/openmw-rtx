#include "shapepass.hpp"

#include <algorithm>
#include <cassert>
#include <functional>

#include <components/contentstore/key.hpp>
#include <components/rtx/shaders/tangent.h>

namespace Rtx
{
    void ShapePass::digest(const Input& input, ContentStore::Digest& digest)
    {
        digest.add(input.mPositions);
        digest.add(input.mNormals);
        digest.add(input.mTriangles);
        digest.add(input.mSplits);
        digest.add(input.mBeside.mPositions);
        digest.add(input.mBeside.mNormals);
        digest.add(input.mBeside.mTriangles);
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
        // takes a light's side from its plane and its shadow ray is not lifted. And foliage averages
        // its cards' normals on purpose, so a clump lights as one volume. No normals is nothing to
        // split either, which the split answers by writing nothing.
        const bool splits = input.mSplits && !output.mShape.mSheet;
        CreaseSplit::Split split{ .mNormals = output.mNormals, .mSources = output.mSources };
        mSplit.split(input.mPositions, splits ? input.mNormals : std::span<const osg::Vec3f>(), output.mKept,
            input.mBeside, split);

        // **The normals as the device's words read them back**, the form the store keeps them in
        // (`ContentCodec<ShapePass::Output>`): a shape is one output whether a store holds it, it was run
        // here, or no store stands at all, where `--content-cache=off` handed the mesh table the floats.
        for (osg::Vec3f& normal : output.mNormals)
            normal = Shaders::unpackNormal(Shaders::packNormal(normal));
    }

    bool ShapePass::fits(const Input& input, const Output& output)
    {
        const std::size_t vertices = input.mPositions.size();
        const std::size_t made = vertices + output.mSources.size();
        return output.mKept.size() % 3 == 0
            && std::ranges::all_of(output.mKept, [made](const std::uint32_t corner) { return corner < made; })
            && std::ranges::all_of(
                output.mSources, [vertices](const std::uint32_t source) { return source < vertices; })
            && (output.mNormals.empty() || output.mNormals.size() == made);
    }
}
