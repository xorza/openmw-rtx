#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/buffer.hpp>

namespace Rtx
{
    class Device;

    /// What one camera's bounce reuse keeps, at one extent: a reservoir and an origin a traced
    /// pixel, this frame's and last frame's, and the path's transmittance in front of each pixel,
    /// which only this frame reads (`bouncereuse.h`). A chain's and not the passes', as the
    /// denoiser's history is (`DenoiseHistory`): the kernels are every chain's, and the history is
    /// as big as the camera it follows.
    ///
    /// **Buffers and not images**, because a reservoir is a record of six words, read whole.
    class BounceReservoirs
    {
    public:
        explicit BounceReservoirs(const Device& device);

        /// Makes room for a frame this size, anew, or for one pixel where the chain never reuses —
        /// a picture's, whose trace still binds them. A resize is a reset.
        void resize(std::uint32_t width, std::uint32_t height, bool reuses);

        /// Says last frame's half is worthless, until a frame that reuses writes it again.
        void reset() { mHistory = false; }

        /// Turns to the other half for the frame being recorded, and says whether the half it now
        /// reads holds last frame's reservoirs: a frame that does not reuse writes none, and the
        /// next one that does starts afresh.
        bool turn(bool reuses);

        /// How many reservoirs a row holds, which every reader indexes by.
        std::uint32_t getStride() const { return mStride; }

        /// The halves this frame writes and last frame wrote, and the transmittance.
        const Buffer& getReservoirs() const { return mReservoirs[mNow]; }
        const Buffer& getReservoirsBefore() const { return mReservoirs[1 - mNow]; }
        const Buffer& getOrigins() const { return mOrigins[mNow]; }
        const Buffer& getOriginsBefore() const { return mOrigins[1 - mNow]; }
        const Buffer& getThrough() const { return mThrough; }

    private:
        const Device& mDevice;

        std::array<Buffer, 2> mReservoirs;
        std::array<Buffer, 2> mOrigins;
        Buffer mThrough;

        std::uint32_t mStride = 0;
        std::size_t mNow = 0;

        /// Whether the half last frame wrote holds reservoirs a frame may read.
        bool mHistory = false;
    };
}
