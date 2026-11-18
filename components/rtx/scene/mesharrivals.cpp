#include "mesharrivals.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Rtx
{
    namespace
    {
        /// `count` more elements at the end of `values`, as a run starting where it was.
        template <class T>
        std::span<T> grow(std::vector<T>& values, const std::uint32_t count)
        {
            const std::size_t start = values.size();
            values.resize(start + count);
            return std::span<T>(values).subspan(start, count);
        }

        template <class T>
        std::size_t capacityBytes(const std::vector<T>& values)
        {
            return values.capacity() * sizeof(T);
        }

        template <class T>
        void empty(std::vector<T>& values, const bool giveBack)
        {
            if (giveBack)
                values = std::vector<T>();
            else
                values.clear();
        }

        template <class T>
        std::span<const T> runOf(const std::vector<T>& values, const std::uint32_t start, const std::uint32_t count)
        {
            return std::span<const T>(values).subspan(start, count);
        }
    }

    MeshArrivals::Filling MeshArrivals::add(const MeshIndex mesh, const Lengths& lengths)
    {
        assert(lengths.mVertices > 0 && "an arrival with no vertices");
        assert((lengths.mSecondTexCoords == 0 || lengths.mSecondTexCoords == lengths.mVertices)
            && "a second set of coordinates not one a vertex");

        mRecordOf.growTo(mesh, sNoRecord);
        if (mRecordOf[mesh] == sNoRecord)
            mHolding.push_back(mesh);
        mRecordOf[mesh] = static_cast<std::uint32_t>(mRecords.size());

        mRecords.push_back(Record{
            .mVertexStart = static_cast<std::uint32_t>(mPositions.size()),
            .mSecondStart = static_cast<std::uint32_t>(mSecondTexCoords.size()),
            .mIndexStart = static_cast<std::uint32_t>(mIndices.size()),
            .mLengths = lengths,
        });

        return Filling{
            .mPositions = grow(mPositions, lengths.mVertices),
            .mNormals = grow(mNormals, lengths.mVertices),
            .mTexCoords = grow(mTexCoords, lengths.mVertices),
            .mColours = grow(mColours, lengths.mVertices),
            .mTangents = grow(mTangents, lengths.mVertices),
            .mSecondTexCoords = grow(mSecondTexCoords, lengths.mSecondTexCoords),
            .mIndices = grow(mIndices, lengths.mIndices),
        };
    }

    void MeshArrivals::forget(const MeshIndex mesh)
    {
        // Left in the buffers until `clear`, which no reader reaches without its record.
        if (holds(mesh))
            mRecordOf[mesh] = sNoRecord;
    }

    MeshGeometry MeshArrivals::of(const MeshIndex mesh) const
    {
        assert(holds(mesh) && "the geometry of a mesh that did not arrive since the last hand-over");
        const Record& record = mRecords[mRecordOf[mesh]];
        const Lengths& lengths = record.mLengths;
        return MeshGeometry{
            .mPositions = runOf(mPositions, record.mVertexStart, lengths.mVertices),
            .mNormals = runOf(mNormals, record.mVertexStart, lengths.mVertices),
            .mTexCoords = runOf(mTexCoords, record.mVertexStart, lengths.mVertices),
            .mColours = runOf(mColours, record.mVertexStart, lengths.mVertices),
            .mTangents = runOf(mTangents, record.mVertexStart, lengths.mVertices),
            .mSecondTexCoords = runOf(mSecondTexCoords, record.mSecondStart, lengths.mSecondTexCoords),
            .mIndices = runOf(mIndices, record.mIndexStart, lengths.mIndices),
        };
    }

    void MeshArrivals::clear()
    {
        for (const MeshIndex mesh : mHolding)
            mRecordOf[mesh] = sNoRecord;
        mHolding.clear();
        mRecords.clear();

        const bool giveBack = getBytes() > sKeptBytes;
        empty(mPositions, giveBack);
        empty(mNormals, giveBack);
        empty(mTexCoords, giveBack);
        empty(mColours, giveBack);
        empty(mTangents, giveBack);
        empty(mSecondTexCoords, giveBack);
        empty(mIndices, giveBack);
    }

    std::size_t MeshArrivals::getBytes() const
    {
        return capacityBytes(mPositions) + capacityBytes(mNormals) + capacityBytes(mTexCoords) + capacityBytes(mColours)
            + capacityBytes(mTangents) + capacityBytes(mSecondTexCoords) + capacityBytes(mIndices);
    }
}
