#include "packedstreams.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/misc/hashstate.hpp>
#include <components/rtx/common/finite.hpp>
#include <components/rtx/common/halffloat.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/scratch.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/tangent.h>

#include "uvdensity.hpp"

namespace Rtx
{
    namespace
    {
        /// A vertex's tangent as the device stores it, `Shaders::TANGENT_*`, from
        /// `osgUtil::TangentSpaceGenerator`'s: a direction, and in `w` the handedness the bitangent
        /// `cross(N, T) * w` is taken with.
        std::uint32_t packTangent(const osg::Vec4f& tangent)
        {
            return Shaders::packTangent(osg::Vec3f(tangent.x(), tangent.y(), tangent.z()), tangent.w() < 0.0f);
        }

        /// `values` appended to `into`, as a run.
        template <class T>
        Run appendRun(std::vector<T>& into, const std::span<const T> values)
        {
            const Run run{ .mOffset = static_cast<std::uint32_t>(into.size()),
                .mCount = static_cast<std::uint32_t>(values.size()) };
            into.insert(into.end(), values.begin(), values.end());
            return run;
        }

        /// `values` packed by `pack` and appended to `into`, as a run.
        template <class T, class From, class Pack>
        Run appendPacked(std::vector<T>& into, const std::span<const From> values, const Pack& pack)
        {
            const Run run{ .mOffset = static_cast<std::uint32_t>(into.size()),
                .mCount = static_cast<std::uint32_t>(values.size()) };
            into.reserve(into.size() + values.size());
            for (const From& value : values)
                into.push_back(pack(value));
            return run;
        }

        template <class T>
        std::size_t roomOf(const std::vector<T>& values)
        {
            return values.capacity() * sizeof(T);
        }

        template <class T>
        std::size_t contentOf(const std::vector<T>& values)
        {
            return values.size() * sizeof(T);
        }
    }

    MeshKey keyOf(const PackedMesh& mesh)
    {
        static_assert(sizeof(FoldedShape) == 3, "a fact of the shape the key does not digest");

        // Each stream a step of its own, so where one ends and the next begins is part of the key, and an empty one
        // moves the state as any other does (`Misc::HashState::add`).
        Misc::HashState hash;
        hash.add(mesh.mPositions);
        hash.add(mesh.mNormals);
        hash.add(mesh.mTexCoords);
        hash.add(mesh.mSecondTexCoords);
        hash.add(mesh.mColours);
        hash.add(mesh.mTangents);
        hash.add(mesh.mIndices);
        hash.add(mesh.mUnitStreams);
        hash.add(static_cast<std::uint8_t>(mesh.mShape.mSheet));
        hash.add(static_cast<std::uint8_t>(mesh.mShape.mFolded));
        hash.add(static_cast<std::uint8_t>(mesh.mShape.mPocketed));
        return MeshKey{ .mWords = hash.getWords() };
    }

    Shaders::GpuVertexColour packColour(const osg::Vec3f& colour)
    {
        const std::uint16_t red = toHalf(colour.x());
        const std::uint16_t green = toHalf(colour.y());
        const std::uint16_t blue = toHalf(colour.z());
        const auto infinite = [](const std::uint16_t half) { return (half & 0x7c00u) == 0x7c00u; };
        if (infinite(red) || infinite(green) || infinite(blue))
            return packColour(osg::Vec3f(1.0f, 1.0f, 1.0f));

        return Shaders::GpuVertexColour{
            .mRedGreen = std::uint32_t{ red } | (std::uint32_t{ green } << 16),
            .mBlue = blue,
        };
    }

    PackedRuns PackedStreams::append(const MeshArrays& arrays, const FoldedShape shape)
    {
        const std::span<const osg::Vec3f> positions = arrays.mPositions;
        assert(!positions.empty());
        assert(arrays.mNormals.empty() || arrays.mNormals.size() == positions.size());
        assert(arrays.mTexCoords.empty() || arrays.mTexCoords.size() == positions.size());
        assert(arrays.mSecondTexCoords.empty() || arrays.mSecondTexCoords.size() == positions.size());
        assert((arrays.mUnitStreams == 0 || !arrays.mSecondTexCoords.empty())
            && "a unit reads a second set the mesh did not bring");
        assert(arrays.mColours.empty() || arrays.mColours.size() == positions.size());
        assert(arrays.mTangents.empty() || arrays.mTangents.size() == positions.size());
        assert(std::ranges::all_of(positions, [](const osg::Vec3f& position) { return isFinite(position); })
            && "a vertex that is not finite, which the reader refuses");
        assert(std::ranges::all_of(arrays.mColours, [](const osg::Vec3f& colour) { return isFinite(colour); })
            && "a colour that is not finite, which the reader reads as white");
        assert(std::ranges::all_of(arrays.mNormals, [](const osg::Vec3f& normal) { return isFinite(normal); })
            && "a normal that is not finite, which the reader reads as none");
        assert(std::ranges::all_of(arrays.mTangents, [](const osg::Vec4f& tangent) { return isFinite(tangent); })
            && "a tangent that is not finite, which the reader reads as none");
        assert(std::ranges::all_of(arrays.mTexCoords, [](const osg::Vec2f& coord) { return isFinite(coord); })
            && "a texture coordinate that is not finite, which the reader reads as none");
        assert(std::ranges::all_of(arrays.mSecondTexCoords, [](const osg::Vec2f& coord) { return isFinite(coord); })
            && "a texture coordinate that is not finite in the second set, which the reader reads as none");
        assert(arrays.mIndices.size() % 3 == 0);
        assert(std::ranges::all_of(arrays.mIndices, [&](const std::uint32_t at) { return at < positions.size(); }));

        PackedRuns runs{
            .mVertices = appendRun(mPositions, positions),
            .mNormals = appendPacked(
                mNormals, arrays.mNormals, [](const osg::Vec3f& normal) { return Shaders::packNormal(normal); }),
            .mTexCoords = appendRun(mTexCoords, arrays.mTexCoords),
            .mSecondTexCoords = appendRun(mSecondTexCoords, arrays.mSecondTexCoords),
            .mColours = appendPacked(mColours, arrays.mColours, packColour),
            .mTangents = appendPacked(mTangents, arrays.mTangents, packTangent),
            .mIndices = appendRun(mIndices, arrays.mIndices),
            .mUnitStreams = arrays.mSecondTexCoords.empty() ? 0u : arrays.mUnitStreams,
            .mSharedIndices = arrays.mSharedIndices,
            .mShape = shape,
            .mBounds = {},
            .mUvDensity = uvDensityOf(positions, arrays.mTexCoords, arrays.mSecondTexCoords, arrays.mIndices),
            .mKey = {},
        };

        // **Tangents that all pack to nought are none**: a generator that found no texture coordinates to build from
        // leaves tangents of no length, and the device reads a word of nought as no tangent either way.
        if (std::ranges::all_of(runs.mTangents.in(std::span<const std::uint32_t>(mTangents)),
                [](const std::uint32_t word) { return word == 0; }))
        {
            mTangents.resize(runs.mTangents.mOffset);
            runs.mTangents.mCount = 0;
        }

        for (const osg::Vec3f& position : positions)
            runs.mBounds.expandBy(position);
        runs.mKey = keyOf(of(runs));
        return runs;
    }

    PackedMesh PackedStreams::of(const PackedRuns& runs) const
    {
        return PackedMesh{
            .mPositions = runs.mVertices.in(std::span<const osg::Vec3f>(mPositions)),
            .mNormals = runs.mNormals.in(std::span<const std::uint32_t>(mNormals)),
            .mTexCoords = runs.mTexCoords.in(std::span<const osg::Vec2f>(mTexCoords)),
            .mSecondTexCoords = runs.mSecondTexCoords.in(std::span<const osg::Vec2f>(mSecondTexCoords)),
            .mUnitStreams = runs.mUnitStreams,
            .mColours = runs.mColours.in(std::span<const Shaders::GpuVertexColour>(mColours)),
            .mTangents = runs.mTangents.in(std::span<const std::uint32_t>(mTangents)),
            .mIndices = runs.mIndices.in(std::span<const std::uint32_t>(mIndices)),
            .mSharedIndices = runs.mSharedIndices,
            .mShape = runs.mShape,
            .mBounds = runs.mBounds,
            .mUvDensity = runs.mUvDensity,
            .mKey = runs.mKey,
        };
    }

    void PackedStreams::clear()
    {
        mPositions.clear();
        mNormals.clear();
        mTexCoords.clear();
        mSecondTexCoords.clear();
        mColours.clear();
        mTangents.clear();
        mIndices.clear();
    }

    void PackedStreams::reuse()
    {
        dropSlack(mPositions);
        dropSlack(mNormals);
        dropSlack(mTexCoords);
        dropSlack(mSecondTexCoords);
        dropSlack(mColours);
        dropSlack(mTangents);
        dropSlack(mIndices);
        clear();
    }

    void PackedStreams::letGo()
    {
        *this = PackedStreams();
    }

    std::size_t PackedStreams::getRoomBytes() const
    {
        return roomOf(mPositions) + roomOf(mNormals) + roomOf(mTexCoords) + roomOf(mSecondTexCoords) + roomOf(mColours)
            + roomOf(mTangents) + roomOf(mIndices);
    }

    std::size_t PackedStreams::getContentBytes() const
    {
        return contentOf(mPositions) + contentOf(mNormals) + contentOf(mTexCoords) + contentOf(mSecondTexCoords)
            + contentOf(mColours) + contentOf(mTangents) + contentOf(mIndices);
    }
}
