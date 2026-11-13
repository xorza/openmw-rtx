#include "memoryreport.hpp"

#include <format>

#include "renderer.hpp"

namespace Rtx
{
    namespace
    {
        /// What compaction has left to give back, or nothing where there is none or the device would not say: left
        /// out rather than printed as nought, which would read as a saving of everything.
        std::string describeCompaction(const SceneStats& scene)
        {
            if (scene.mCompactableBytes == 0 || scene.mCompactableNowBytes <= scene.mCompactableBytes)
                return {};

            return std::format(" ({:.1f} of them would compact to {:.1f})", mebibytes(scene.mCompactableNowBytes),
                mebibytes(scene.mCompactableBytes));
        }

        /// How many textures stand smaller than their files, and nothing where none does: a place
        /// drawn as its files are says nothing about it.
        std::string describeReduced(const SceneStats& scene)
        {
            if (scene.mReducedTextureCount == 0)
                return {};

            return std::format(", {} of them held smaller", scene.mReducedTextureCount);
        }
    }

    std::string describeMemory(const MemoryReport& report)
    {
        std::string out;
        for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
        {
            const HeapUse& use = report.mHeaps[heap];

            // The system's heap is named, because on a card with video memory of its own it can be the
            // largest, and read as video memory it is a card with more room than it has. The index
            // takes two places, which sixteen heaps need.
            out += std::format(
                "  heap {:>2}  {}  {:8.1f} MiB   ", heap, use.mDeviceLocal ? "device" : "system", mebibytes(use.mSize));
            out += std::format("reserved {:7.1f}   live {:7.1f}", mebibytes(use.mReserved), mebibytes(use.mLive));

            // Nought from a driver that would not say, which is not the same answer as a budget of
            // none — so the columns are left off rather than printed as zeroes.
            if (use.mBudget > 0)
                out += std::format("   budget {:7.1f}   held {:7.1f}", mebibytes(use.mBudget), mebibytes(use.mHeld));

            out += std::format("   {} blocks\n", use.mBlocks);
        }

        // What the video heap's `live` is made of, and why content stops where it does: the room
        // kept for the frame's largest targets, and what each kind of content may still take.
        const MemoryUses& uses = report.mUses;
        if (uses.mFrame + uses.mFrameReserve + uses.mEssential + uses.mStructures + uses.mTextures > 0)
            out += std::format(
                "  uses  frame {:.1f} of {:.1f} kept   essential {:.1f}   structures {:.1f}, room {:.1f}   "
                "textures {:.1f}, room {:.1f}\n",
                mebibytes(uses.mFrame), mebibytes(uses.mFrameReserve), mebibytes(uses.mEssential),
                mebibytes(uses.mStructures), mebibytes(uses.mStructureRoom), mebibytes(uses.mTextures),
                mebibytes(uses.mTextureRoom));

        return out;
    }

    std::string describeSceneHeld(const SceneStats& scene)
    {
        return std::format(
            "{} instances ({} cutouts) in {} slots   {:.1f} MiB structures in {:.1f} reserved{}, {:.1f} MiB "
            "tables, {:.1f} MiB working   {} textures, "
            "{:.1f} MiB{}: files {:.1f}, bakes {:.1f}, ground {:.1f}, gloss {:.1f}",
            scene.mInstances.mPlaced, scene.mInstances.mCutout, scene.mInstanceSlots,
            mebibytes(scene.mStructureLiveBytes), mebibytes(scene.mStructureBytes), describeCompaction(scene),
            mebibytes(scene.mTableBytes), mebibytes(scene.mWorkBytes), scene.mTextureCount,
            mebibytes(scene.mTextureBytes), describeReduced(scene), mebibytes(scene.mTextureBytesBySource.mFiles),
            mebibytes(scene.mTextureBytesBySource.mSpriteBakes),
            mebibytes(scene.mTextureBytesBySource.mGroundComposites),
            mebibytes(scene.mTextureBytesBySource.mGroundGloss));
    }
}
