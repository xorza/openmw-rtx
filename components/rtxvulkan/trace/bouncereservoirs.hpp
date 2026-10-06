#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/buffer.hpp>

namespace Rtx
{
    class Device;

    /// What one camera's bounce reuse keeps, at one extent: a traced pixel's reservoir this frame,
    /// the history the next frame merges, its visible point this frame and last frame, and the
    /// path's transmittance in front of it, which only this frame reads (`bouncereuse.h`). A
    /// chain's and not the passes', as the denoiser's history is (`DenoiseHistory`): the kernels
    /// are every chain's, and the history is as big as the camera it follows.
    ///
    /// **Buffers and not images**, because a reservoir is a record of eight words, read whole.
    class BounceReservoirs
    {
    public:
        explicit BounceReservoirs(const Device& device);

        /// Makes room for a frame this size, anew: for one pixel, which a trace that reuses nothing
        /// binds, until a frame asks for a reuse (`demand`), and for every pixel after that. Where
        /// the chain never reuses — a picture's — for one pixel always. A resize is a reset.
        void resize(std::uint32_t width, std::uint32_t height, bool reuses);

        /// Makes room for every pixel, and the spatial reuse's pairings for the height, waiting for
        /// their upload, the first time a frame asks for a reuse. **On demand, and not at the
        /// resize**, because the game's default reuses nothing, and the reservoirs, the visible
        /// points and the rest are 136 bytes a traced pixel, 120 MiB where 1280 by 720 are traced,
        /// that nothing reads. What the frames in flight bound goes to the graveyard, which keeps it
        /// to the end of their reads.
        void demand();

        /// Whether the buffers stand for every pixel of the extent: what a frame that reuses needs.
        bool holdsEveryPixel() const { return mFull; }

        /// Says last frame's half is worthless, until a frame that reuses writes it again.
        void reset() { mHistoryKept = false; }

        /// Turns the visible points to the other half for the frame being recorded, and says whether
        /// the history holds last frame's reservoirs: a frame that does not reuse writes none, and the
        /// next one that does starts afresh.
        bool turn(bool reuses);

        /// How many reservoirs a row holds, which every reader indexes by.
        std::uint32_t getStride() const { return mStride; }

        /// This frame's reservoirs and the history; the visible points this frame writes and last
        /// frame wrote; and the transmittance.
        const Buffer& getReservoirs() const { return mReservoirs; }
        const Buffer& getHistory() const { return mHistory; }
        const Buffer& getOrigins() const { return mOrigins[mNow]; }
        const Buffer& getOriginsBefore() const { return mOrigins[1 - mNow]; }
        const Buffer& getThrough() const { return mThrough; }

        /// Both pairing textures' steps, one after the other (`BouncePairing`), and the pairs' bits.
        const Buffer& getPairing() const { return mPairing; }
        const Buffer& getPaired() const { return mPaired; }

    private:
        const Device& mDevice;

        /// **One of each and not a pair**, because the history is written last, by the resolve, after
        /// every reader of last frame's is done: the trace and the temporal pass fill this frame's,
        /// and the resolve leaves in the history what the next frame merges.
        Buffer mReservoirs;
        Buffer mHistory;
        std::array<Buffer, 2> mOrigins;
        Buffer mThrough;
        Buffer mPairing;
        Buffer mPaired;

        /// Makes the buffers for every pixel of the extent, or for one.
        void allocate(bool full);

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::uint32_t mStride = 0;
        std::size_t mNow = 0;

        /// Whether the chain may reuse at all, and whether a frame asked it to since the resize.
        bool mReuses = false;
        bool mDemanded = false;
        bool mFull = false;

        /// Whether the history holds reservoirs a frame may read.
        bool mHistoryKept = false;
    };
}
