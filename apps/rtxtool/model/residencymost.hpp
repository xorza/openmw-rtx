#pragma once

#include <algorithm>
#include <cstdint>

#include <components/rtx/renderer/residency.hpp>

namespace RtxTool
{
    /// **The most a place's frames' residency came to** (`Rtx::ResidencyReport`), frame by frame, where a sample at the
    /// place's end misses what a frame between did: the work a frame issued, the residents short of what they want, and
    /// the arena's slots and bytes, each its own most; the cap its least.
    struct ResidencyMost
    {
        std::uint32_t mOpsIssued = 0;
        std::uint64_t mBytesUploaded = 0;
        std::uint64_t mBytesCopied = 0;
        std::uint32_t mBelowWant = 0;

        /// Demanded textures drawing the stand-in, and demanded meshes placing nothing.
        std::uint32_t mStandIns = 0;
        std::uint32_t mMeshesMissing = 0;
        std::uint32_t mStarvedTextures = 0;
        std::uint32_t mStarvedStructures = 0;
        std::uint32_t mSlotsLive = 0;
        std::uint32_t mSlotsRetiring = 0;
        std::uint64_t mBytesHeld = 0;

        /// The blocks the device refused over the place's frames.
        std::uint32_t mBlocksRefused = 0;

        /// The least cap any frame stood under, nought for none taken.
        std::uint64_t mCapBytesLeast = 0;

        /// How many frames it took.
        std::uint32_t mFrames = 0;

        void take(const Rtx::ResidencyReport& report)
        {
            mOpsIssued = std::max(mOpsIssued, report.mOpsIssued);
            mBytesUploaded = std::max(mBytesUploaded, report.mBytesUploaded);
            mBytesCopied = std::max(mBytesCopied, report.mBytesCopied);
            mBelowWant = std::max(mBelowWant, report.mBelowWant);
            const Rtx::MissingDemanded& missing = report.mMissingDemanded;
            mStandIns = std::max(mStandIns, missing.textures());
            mMeshesMissing = std::max(mMeshesMissing, missing.mMeshes);
            mStarvedTextures = std::max(mStarvedTextures, report.mStarvedTextures);
            mStarvedStructures = std::max(mStarvedStructures, report.mStarvedStructures);
            mSlotsLive = std::max(mSlotsLive, report.mSlots.mSlotsLive);
            mSlotsRetiring = std::max(mSlotsRetiring, report.mSlots.mSlotsRetiring);
            mBytesHeld = std::max(mBytesHeld, report.mSlots.mBytesHeld);
            mBlocksRefused += report.mBlocksRefused;
            mCapBytesLeast = mFrames == 0 ? report.mCapBytes : std::min(mCapBytesLeast, report.mCapBytes);
            ++mFrames;
        }
    };
}
