#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    /// One mesh's geometry in the device's form, as a hand-over writes it: each stream a run as long as the mesh's
    /// vertices, the second texture coordinates empty where it brought none.
    struct MeshGeometry
    {
        std::span<const osg::Vec3f> mPositions;

        /// `Shaders::packNormal`'s words, nought where the mesh brought none.
        std::span<const std::uint32_t> mNormals;
        std::span<const osg::Vec2f> mTexCoords;
        std::span<const Shaders::GpuVertexColour> mColours;

        /// `Shaders::packTangent`'s words, nought where the mesh brought none.
        std::span<const std::uint32_t> mTangents;
        std::span<const osg::Vec2f> mSecondTexCoords;

        /// Empty where the mesh names a run of shared indices that another arrival or an earlier hand-over brought
        /// (`MeshArrays::mSharedIndices`): the device holds them, or this hand-over writes them once.
        std::span<const std::uint32_t> mIndices;
    };

    /// **The geometry of the meshes that arrived since the last hand-over**, the host's one copy of it: one flat
    /// buffer a stream, a record a mesh, emptied by `clear` once the device has it (`docs/architecture.md`, "The
    /// geometry has one host copy, for one hand-over"). Bounded by the arrivals of one hand-over, at most every
    /// standing mesh at a world's first; what it keeps between hand-overs is bounded by `sKeptBytes`.
    class MeshArrivals
    {
    public:
        /// What the buffers keep of their capacity past `clear`, so a crossing's arrivals allocate nothing: a world's
        /// first hand-over, hundreds of mebibytes of it, is given back.
        static constexpr std::size_t sKeptBytes = std::size_t{ 64 } << 20;

        /// How long each of an arrival's runs is.
        struct Lengths
        {
            std::uint32_t mVertices = 0;
            std::uint32_t mSecondTexCoords = 0;
            std::uint32_t mIndices = 0;
        };

        /// An arrival's runs, for its mesh to fill whole before anything else arrives.
        struct Filling
        {
            std::span<osg::Vec3f> mPositions;
            std::span<std::uint32_t> mNormals;
            std::span<osg::Vec2f> mTexCoords;
            std::span<Shaders::GpuVertexColour> mColours;
            std::span<std::uint32_t> mTangents;
            std::span<osg::Vec2f> mSecondTexCoords;
            std::span<std::uint32_t> mIndices;
        };

        /// Room for `mesh`'s geometry at `lengths`, standing for any arrival of the slot before it.
        Filling add(MeshIndex mesh, const Lengths& lengths);

        /// Lets go of `mesh`'s arrival, a mesh freed before its hand-over: nothing reads it again.
        void forget(MeshIndex mesh);

        bool holds(MeshIndex mesh) const { return mRecordOf.holds(mesh) && mRecordOf[mesh] != sNoRecord; }

        /// `mesh`'s geometry, which must have arrived since the last `clear`.
        MeshGeometry of(MeshIndex mesh) const;

        /// Empties the buffers once the device has what they hold, keeping their capacity up to `sKeptBytes`.
        void clear();

        /// What the buffers take of the host, their capacity.
        std::size_t getBytes() const;

    private:
        struct Record
        {
            std::uint32_t mVertexStart = 0;
            std::uint32_t mSecondStart = 0;
            std::uint32_t mIndexStart = 0;
            Lengths mLengths;
        };

        std::vector<Record> mRecords;

        /// Each mesh slot's record, `sNoRecord` where it has none: as long as the highest slot that ever arrived.
        RowVector<std::uint32_t, MeshIndex> mRecordOf;
        static constexpr std::uint32_t sNoRecord = ~std::uint32_t{ 0 };

        /// The slots that hold a record, which `clear` sets back.
        std::vector<MeshIndex> mHolding;

        std::vector<osg::Vec3f> mPositions;
        std::vector<std::uint32_t> mNormals;
        std::vector<osg::Vec2f> mTexCoords;
        std::vector<Shaders::GpuVertexColour> mColours;
        std::vector<std::uint32_t> mTangents;
        std::vector<osg::Vec2f> mSecondTexCoords;
        std::vector<std::uint32_t> mIndices;
    };
}
