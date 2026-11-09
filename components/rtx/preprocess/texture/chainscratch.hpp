#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <components/rtx/image/chainbuild.hpp>

namespace Rtx
{
    /// A buffer a chain's make holds that grows with its image (`ChainScratch`).
    enum class ChainBuffer : std::uint8_t
    {
        /// The file's levels laid where they are not spanned in place (`describeImage`).
        File,
        /// `ChainBuild::Scratch`'s five.
        Alpha,
        Texels,
        Band,
        Ring,
        Columns,
        /// The chain made, which its caller holds.
        Made,
        /// The payload the store is handed, a copy of the made chain that the store frees once it wrote
        /// it: held for the make alone, never kept.
        Payload,
        /// A conversion's own: both files laid, the `_spec`'s exponents, its band and its highlights, and
        /// the converted level.
        DiffuseFile,
        SpecularFile,
        Gloss,
        SpecularBand,
        Highlights,
        Converted,
        Count,
    };

    /// The bytes of each buffer a chain's or a conversion's make holds: what a thread keeps between
    /// makes (`ContentPreprocessor::chainScratch`), and what one make needs at most (`ChainPass::scratchOf`,
    /// `ConversionPass::scratchOf`), so a dispatcher counts what a make adds past what its thread keeps,
    /// buffer by buffer.
    struct ChainScratch
    {
        std::array<std::size_t, static_cast<std::size_t>(ChainBuffer::Count)> mBytes{};

        std::size_t& operator[](ChainBuffer buffer) { return mBytes[static_cast<std::size_t>(buffer)]; }
        std::size_t operator[](ChainBuffer buffer) const { return mBytes[static_cast<std::size_t>(buffer)]; }

        std::size_t total() const
        {
            std::size_t sum = 0;
            for (const std::size_t bytes : mBytes)
                sum += bytes;
            return sum;
        }

        /// What a make that needs `need` adds past this, which a thread keeps: each buffer's need past what
        /// it holds.
        std::size_t growthTo(const ChainScratch& need) const
        {
            std::size_t growth = 0;
            for (std::size_t at = 0; at < mBytes.size(); ++at)
                growth += need.mBytes[at] - std::min(need.mBytes[at], mBytes[at]);
            return growth;
        }

        /// Each buffer the larger of the two: what a thread keeps after a make of `other`.
        ChainScratch& keepLarger(const ChainScratch& other)
        {
            for (std::size_t at = 0; at < mBytes.size(); ++at)
                mBytes[at] = std::max(mBytes[at], other.mBytes[at]);
            return *this;
        }

        /// Whether no buffer holds more than `bound`'s: a make whose scratch passed its prediction.
        bool within(const ChainScratch& bound) const
        {
            for (std::size_t at = 0; at < mBytes.size(); ++at)
                if (mBytes[at] > bound.mBytes[at])
                    return false;
            return true;
        }

        /// `ChainBuild::Scratch`'s buffers, as theirs.
        void setBuild(const ChainBuild::Scratch& build)
        {
            (*this)[ChainBuffer::Alpha] = build.mAlpha;
            (*this)[ChainBuffer::Texels] = build.mTexels;
            (*this)[ChainBuffer::Band] = build.mBand;
            (*this)[ChainBuffer::Ring] = build.mRing;
            (*this)[ChainBuffer::Columns] = build.mColumns;
        }
    };
}
