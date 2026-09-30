#pragma once

#include <cstdint>
#include <memory>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/trace/denoise/denoisehistory.hpp>

#include "fogvolume.hpp"
#include "gbuffer.hpp"
#include "spritebin.hpp"

namespace Rtx
{
    class Device;
    struct TracePasses;
    struct TraceRecording;
    struct TraceResult;
    struct VisibilityInputs;

    /// Everything one camera's trace writes, at one extent — one chain however many cameras have
    /// one, so a barrier cannot go missing from a second copy. What differs between two of these
    /// is what the caller hands in: the extent, and whether the chain is sized exactly or grown to
    /// fit. What becomes of a finished picture — the frame's presented pair, a picture's byte
    /// target — is not here.
    class TraceChain
    {
    public:
        /// Nothing has an extent until `resize` or `grow` is called.
        ///
        /// @param passes what the chain traces with, which outlives it.
        TraceChain(const Device& device, const TracePasses& passes);

        /// Builds the chain at exactly this extent, whatever it was before.
        ///
        /// @param radiance how wide the radiance channels and the frame composed from them are
        ///        stored — the run's choice, which `Rtx::RadianceWidth` argues.
        void resize(std::uint32_t width, std::uint32_t height, RadianceWidth radiance);

        /// Makes the chain at least this big, keeping whatever extent it already reached on either
        /// axis. Nothing where it already `holds` the size. Grown and never shrunk, because a
        /// smaller picture uses a corner of a larger one's images rather than rebuilding them.
        void grow(std::uint32_t width, std::uint32_t height, RadianceWidth radiance);

        /// The extent the images are at, which is what a dispatch over the whole of one covers.
        /// Nought until the first `resize` or `grow`.
        std::uint32_t getWidth() const { return mWidth; }
        std::uint32_t getHeight() const { return mHeight; }

        /// Whether the images exist, which is the same question as whether the extent is set.
        bool isBuilt() const { return mChannels != nullptr; }

        /// Whether a picture this big fits what is built, which is what `grow` would leave alone.
        bool holds(std::uint32_t width, std::uint32_t height) const
        {
            return isBuilt() && width <= mWidth && height <= mHeight;
        }

        /// The frame: one picture in linear radiance, before anything upscales it and before the
        /// display curve. The direct channel, which the trace or the composite composes it into.
        const Image& getColour() const { return mChannels->get(Channel::Direct); }

        /// What the trace writes and the composite reads: one picture's light, still in pieces.
        const GBuffer& getChannels() const { return *mChannels; }

        /// Records one camera's whole trace, from the discards it opens with to the barrier after
        /// the composite, and hands back what the display reads of it. What the caller keeps is
        /// what a frame has and a picture has not — the frame ring, the upscaler, the lens, the
        /// measured exposure and the display curve.
        TraceResult record(VkCommandBuffer commands, const TraceRecording& what);

        /// Says every denoiser's history and the air's are worthless, each until the next trace that
        /// reads it: the air is read by every trace, and the denoisers' only where the wavelet runs.
        void resetHistory();

        /// Lets go of the running total, which a new scene or a new size has no use for: a sum over
        /// one scene means nothing over the next. The first frame that averages makes another.
        void dropSum() { mSum = Image(); }

    private:
        /// The sprite tile list the trace of `inputs` reads: the media's list of nothing for a camera
        /// that draws no sprites, and the slot's bin otherwise. Asked once, after the bin's `take`,
        /// which may have grown the table.
        VkDeviceAddress getSpriteTileList(const VisibilityInputs& inputs) const;

        const Device& mDevice;
        const TracePasses& mPasses;

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        std::unique_ptr<GBuffer> mChannels;
        std::unique_ptr<FogVolume> mFogVolume;

        /// One sprite bin per frame in flight — `VisibilityInputs::mTraceSlot` picks — so the frame
        /// behind keeps the tables its trace reads while this frame's bin writes its own.
        PerSlot<SpriteBin> mBins;

        /// What the shared denoising passes keep of this camera, at the extent.
        DenoiseHistory mDenoise;

        /// Set by `resetHistory` and spent by the next trace, which always integrates the air.
        bool mAirStale = false;

        /// The running sum a reference is built out of, empty until a trace averages. Not a history
        /// and nothing here reprojects: a plain per-pixel total over however many frames the caller
        /// asked to average. Whether it exists is also whether anything has written it, because the
        /// trace that makes one fills it.
        Image mSum;
    };
}
