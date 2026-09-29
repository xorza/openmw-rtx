#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    class Device;

    /// One camera's images for `ShadowPass`, at one extent: what the last frame left for this one,
    /// and where this one's levels go. A chain's and not the pass's, because the pass is a set of
    /// pipelines every chain shares and a history is as big as the camera it follows.
    class ShadowHistory
    {
    public:
        explicit ShadowHistory(const Device& device);

        /// Makes room for a frame this size, if the last one was not. A resize is a reset. The
        /// caller has waited for anything still reading the old images.
        void resize(std::uint32_t width, std::uint32_t height);

        /// Says the history is worthless, until the next `turn`.
        void reset() { mFresh = true; }

        /// What one frame reads and writes.
        ///
        /// **Five images of a frame's own and two that carry over**, the SDK's arrangement: the mask
        /// pass packs the rays' bits, the temporal pass writes its blend into the scratch, the first filter level
        /// writes the history the next frame's temporal pass reads, the second writes the scratch again — where a
        /// cleared tile keeps the temporal pass's exact value — and the third writes what the composite reads.
        struct Turn
        {
            const Image& mMomentsBefore;
            const Image& mMoments;
            const Image& mHistory;
            const Image& mScratch;
            const Image& mVisibility;
            const Image& mTiles;
            const Image& mMask;

            /// The first frame after a `resize` or a `reset`, whose history is worthless.
            bool mFresh = false;
        };

        /// Turns to the other half of the moments for the frame being recorded.
        Turn turn();

        std::uint32_t getWidth() const { return mVisibility.getWidth(); }
        std::uint32_t getHeight() const { return mVisibility.getHeight(); }

    private:
        const Device& mDevice;

        /// Two, because this frame reads what the last one wrote. Empty until `resize`.
        std::array<Image, 2> mMoments;

        Image mHistory;
        Image mScratch;
        Image mVisibility;

        /// One texel a tile of the classification, `SHADOW_WORKGROUP` pixels on a side.
        Image mTiles;

        /// One word an 8×4 tile of the rays' bits.
        Image mMask;

        /// Which of the moments this frame writes. Flipped by `turn`.
        std::size_t mCurrent = 0;

        bool mFresh = true;
    };
}
