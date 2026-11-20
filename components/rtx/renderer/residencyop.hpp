#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slotledger.hpp>

namespace Rtx
{
    /// Which request of the feed an operation's texels came back as (`TextureFeed`).
    enum class FeedTicket : std::uint32_t
    {
    };

    /// A run of a chain's levels, `mFirst` up to and not including `mEnd`, level 0 the finest.
    struct LevelSpan
    {
        std::uint8_t mFirst = 0;
        std::uint8_t mEnd = 0;

        bool empty() const { return mFirst >= mEnd; }

        friend bool operator==(const LevelSpan&, const LevelSpan&) = default;
    };

    /// **An image stood in a slot**: an arrival, a raise, a lowering or a move out of a draining slab, one operation.
    /// Its levels from `mLevel` down; what stands at `mFrom` already is copied on the device where it shares a level,
    /// and the levels `mUpload` names come from the feed's texels for `mFeed`.
    struct StandImage
    {
        TextureIndex mTexture = sNoRow;
        std::uint32_t mGeneration = 0;
        Slot mTo;
        std::uint8_t mLevel = 0;

        /// What stands now and the level it stands from, whose shared levels are copied: none for an arrival.
        std::optional<Slot> mFrom{};
        std::uint8_t mFromLevel = 0;

        /// The finer levels, in the feed's texels for `mFeed`: empty for a lowering or a move.
        LevelSpan mUpload{};
        std::optional<FeedTicket> mFeed{};

        friend bool operator==(const StandImage&, const StandImage&) = default;
    };

    /// An image's slot given back: what reads it in a frame in flight still may, until the epoch the hand-over rides.
    struct DropImage
    {
        TextureIndex mTexture = sNoRow;
        Slot mFrom;

        friend bool operator==(const DropImage&, const DropImage&) = default;
    };

    /// A mesh's structure built into a slot, loose: what a tight size is asked of.
    struct BuildStructure
    {
        MeshIndex mMesh = sNoRow;
        std::uint32_t mGeneration = 0;
        Slot mLoose;

        /// What the device priced the build at (`ResidencyDevice::priceStructure`), which the slot holds.
        std::uint64_t mBytes = 0;

        friend bool operator==(const BuildStructure&, const BuildStructure&) = default;
    };

    /// How a structure is copied: made tight, or moved whole out of a draining slab.
    enum class StructureCopy : std::uint8_t
    {
        Compact,
        Clone,
    };

    struct CopyStructure
    {
        MeshIndex mMesh = sNoRow;
        std::uint32_t mGeneration = 0;
        Slot mFrom;
        Slot mTo;
        StructureCopy mKind = StructureCopy::Compact;

        friend bool operator==(const CopyStructure&, const CopyStructure&) = default;
    };

    struct DropStructure
    {
        MeshIndex mMesh = sNoRow;
        Slot mFrom;

        friend bool operator==(const DropStructure&, const DropStructure&) = default;
    };

    /// **What residency hands a device, in the order it is to be done**: drops, lowerings, moves, then arrivals and
    /// raisings, the most protected first. Each names the slot it stands in, which the core reserved, so none can be
    /// refused for room.
    using ResidencyOp = std::variant<StandImage, DropImage, BuildStructure, CopyStructure, DropStructure>;
}
