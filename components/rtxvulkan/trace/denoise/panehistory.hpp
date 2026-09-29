#pragma once

#include <array>
#include <cstdint>

#include <components/rtxvulkan/device/memory/image.hpp>

#include "historyturns.hpp"

namespace Rtx
{
    class Device;

    /// One camera's images for `PanePass`, at one extent: the mean with its frame count, and the
    /// layer it belongs to, as the last frame left them, and the two this one writes. A chain's and
    /// not the pass's, for the reason `ShadowHistory` gives.
    class PaneHistory
    {
    public:
        explicit PaneHistory(const Device& device);

        /// Makes room for a frame this size, if the last one was not. A resize is a reset. The
        /// caller has waited for anything still reading the old images.
        void resize(std::uint32_t width, std::uint32_t height);

        /// Says the history is worthless, until the next `turn`.
        void reset() { mTurns.reset(); }

        /// What one frame reads and writes.
        struct Turn
        {
            const Image& mMeanBefore;
            const Image& mHeldBefore;
            const Image& mMean;
            const Image& mHeld;

            /// The first frame after a `resize` or a `reset`, whose history is worthless.
            bool mFresh = false;
        };

        /// Turns to the other half of each pair for the frame being recorded.
        Turn turn();

    private:
        const Device& mDevice;

        /// Two of each, because this frame reads what the last one wrote. Empty until `resize`.
        std::array<Image, 2> mMeans;
        std::array<Image, 2> mHeld;

        HistoryTurns mTurns;
    };
}
