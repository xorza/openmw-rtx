#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <components/rtx/common/deviceepoch.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/image/imageshape.hpp>
#include <components/rtx/preprocess/texturefeed.hpp>
#include <components/rtx/scene/refusal.hpp>

#include "residencyop.hpp"
#include "slot.hpp"

namespace Rtx
{
    class SceneDesc;

    /// What a mesh's structure is, as a device prices its first build.
    struct StructureShape
    {
        std::uint32_t mTriangles = 0;
        std::uint32_t mVertices = 0;

        /// Built to be refitted, which a device builds looser and never makes tight.
        bool mRefits = false;

        friend bool operator==(const StructureShape&, const StructureShape&) = default;
    };

    /// What a device charges for one resource: its bytes, and the alignment its range starts at.
    struct ContentPrice
    {
        std::uint64_t mBytes = 0;
        std::uint64_t mAlignment = 1;

        friend bool operator==(const ContentPrice&, const ContentPrice&) = default;
    };

    /// A loose structure's size once made tight, as the device answered it.
    struct TightSize
    {
        SceneSlot mScene;
        MeshIndex mMesh = sNoRow;
        std::uint32_t mGeneration = 0;
        std::uint64_t mBytes = 0;

        friend bool operator==(const TightSize&, const TightSize&) = default;
    };

    /// What the device finished and what it found since the frame before was opened.
    struct DeviceEvents
    {
        /// Every submit up to it is done reading.
        DeviceEpoch mFinished;
        std::vector<TightSize> mTightSizes;
        std::vector<BlockIndex> mBlocksArrived;

        /// The device had none of these.
        std::vector<BlockIndex> mBlocksRefused;

        void clear()
        {
            mTightSizes.clear();
            mBlocksArrived.clear();
            mBlocksRefused.clear();
        }
    };

    /// What content may take of the device this frame (`docs/architecture.md`, "The arena's cap").
    struct ContentBudget
    {
        std::uint64_t mCapBytes = 0;
    };

    /// **What a device does for residency, and nothing it decides**: prices what content stands in, says what finished
    /// and what it found, stands what an operation names in the slot it names, and acquires and releases blocks. The
    /// core keeps every slot (`SlotLedger`) and every decision (`Residency`), so a device that is no card keeps the
    /// same contract and the policy is tested with none (`NullDevice`).
    class ResidencyDevice
    {
    public:
        /// Which arena each kind stands in, read once as the layout is made.
        virtual ArenaIndex arenaOf(SlotKind kind) const = 0;

        /// What an image of `shape` and a structure of `shape` take, or nothing where the device holds none.
        virtual std::optional<ContentPrice> priceImage(const ImageShape& shape) const = 0;
        virtual ContentPrice priceStructure(const StructureShape& shape) const = 0;

        /// Whether the device stands meshes' structures in the arena's slots, as `BuildStructure` names them; a
        /// device that does not keeps them in memory of its own, outside the cap, and is handed no structure's
        /// operation.
        virtual bool standsStructures() const = 0;

        /// Reads the budget and fills `events`, which the caller keeps and this clears first.
        virtual ContentBudget openFrame(DeviceEvents& events) = 0;

        /// Appends what arrived in `scene`, does `ops` in order and places it: one submit, whose epoch it answers.
        virtual DeviceEpoch handOver(SceneSlot slot, const SceneDesc& scene, std::span<const ResidencyOp> ops) = 0;

        /// Waits until every block asked is answered: what a run that compares frames calls before a frame opens, so
        /// two processes take each block on one frame whatever the device's thread's timing.
        virtual void awaitBlocks() = 0;

        /// Asks for a block of `bytes` in `arena`, answered by an event; gives back one nothing stands in.
        virtual void acquireBlock(BlockIndex block, ArenaIndex arena, std::uint64_t bytes) = 0;
        virtual void releaseBlock(BlockIndex block) = 0;

        ResidencyDevice(const ResidencyDevice&) = delete;
        ResidencyDevice& operator=(const ResidencyDevice&) = delete;

    protected:
        ResidencyDevice() = default;
        ~ResidencyDevice() = default;
    };
}
