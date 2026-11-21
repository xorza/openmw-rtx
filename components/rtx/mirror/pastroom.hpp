#pragma once

#include <cstdint>
#include <string>

namespace Rtx
{
    /// How many entries arrived past the room of each table the world's mirror fills, ever: each a rehash on the
    /// frame it happened. Nought is what the rooms promise, and a harness's `rooms-held` names the table that grew.
    struct PastRoom
    {
        std::uint32_t mPlacements = 0;
        std::uint32_t mMeshes = 0;
        std::uint32_t mMaterials = 0;
        std::uint32_t mEmitters = 0;
        std::uint32_t mFrozen = 0;
        std::uint32_t mChainKeys = 0;
        std::uint32_t mGlows = 0;

        /// The ring's models (`CellStreamer::countPastRoom`), which a walk's extractor leaves at nought.
        std::uint32_t mRingModels = 0;

        std::uint32_t total() const
        {
            return mPlacements + mMeshes + mMaterials + mEmitters + mFrozen + mChainKeys + mGlows + mRingModels;
        }

        bool operator==(const PastRoom& other) const = default;
    };

    /// What `rooms-held` reports: each table that grew past its room and how often, in the struct's order, or that
    /// none did.
    std::string describe(const PastRoom& past);
}
