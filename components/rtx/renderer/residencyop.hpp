#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/preprocess/texturefeed.hpp>

namespace Rtx
{
    /// **Where a slot stands**: the slot, and the block and offset its bytes are at, which the ledger says as the slot
    /// is reserved, so a device maps an operation to memory with no ledger of its own.
    struct SlotPlace
    {
        Slot mSlot;
        BlockIndex mBlock = sNoRow;
        std::uint64_t mOffset = 0;

        friend bool operator==(const SlotPlace&, const SlotPlace&) = default;
    };

    /// **An image stood in a slot**: an arrival, a raise, a lowering, a move out of a draining slab or a bake, one
    /// operation. Its levels from `mLevel` down; what stands at `mFrom` already is copied on the device where it shares
    /// a level, and the levels `mUpload` names come from `mTexels`.
    struct StandImage
    {
        TextureIndex mTexture = sNoRow;
        std::uint32_t mGeneration = 0;
        SlotPlace mTo;
        std::uint8_t mLevel = 0;

        /// What stands now and the level it stands from, whose shared levels are copied: none for an arrival.
        std::optional<Slot> mFrom{};
        std::uint8_t mFromLevel = 0;

        /// The finer levels, out of `mTexels`: empty for a lowering or a move.
        LevelSpan mUpload{};

        /// What the levels are made of, which stays valid until the hand-over returns: a file's texels from the feed,
        /// or a bake's description, which the device writes from its sources. None for a lowering or a move.
        const TextureData* mTexels = nullptr;

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
        SlotPlace mLoose;

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
        SlotPlace mTo;
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
