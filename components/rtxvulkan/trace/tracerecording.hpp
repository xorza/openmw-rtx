#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/shaders/visibility.h>

#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>

#include "spritebin.hpp"
#include "visibilitypass.hpp"

namespace Rtx
{
    class GpuTimer;
    class Image;

    /// What one camera's trace records against, and what makes this trace different from the
    /// other: a frame and a picture inside the interface record one chain, and what they share is
    /// this list rather than an object. Nothing here is held.
    struct TraceRecording
    {
        /// What the rays meet, where the sea and the sprites the trace reads were left, and what
        /// every launch binds beside them: the census. The chain's own images are the chain's to
        /// name (`TraceChain::record`).
        TraceSubject mSubject;

        /// Which of the chain's sprite bins this trace records into and reads: the frame's own
        /// slot in the world's chain, so the frame behind keeps its bin, and the first in the
        /// pictures' chain. Not which copy of the air it writes, which is a turn a trace
        /// (`FogVolume::turn`) and so stays paired across a frame that traced nothing.
        FrameSlot mBinSlot;

        /// What the sprite bin tiles against, off the camera the caller asked for (`BinCamera`).
        BinCamera mAsked;

        /// The camera as this trace will sample it — the jitter, the previous basis, the medium and
        /// the layer decision are already in it. What the composite covers is its extent. Borrowed:
        /// the frame block is copied once, where the trace writes it (`VisibilityPass::writeFrame`).
        const Shaders::VisibilityConstants& mSampled;

        /// What puts this trace back together, of which the chain reads whether the denoisers run,
        /// what the trace makes of its bounce, and so whether the trace composes the frame itself.
        /// Borrowed, as `mSampled` is.
        const Reconstruction& mReconstruction;

        /// How many frames the chain's running total holds, this one included, or nought where
        /// nothing is averaging (`FrameOptions::mAccumulate`).
        std::uint32_t mAccumulate = 0;

        /// Whether the camera has no past to reproject from: a picture never has one, and a frame
        /// after a jump no motion vector can describe has lost it. Then `mSampled` carries a basis
        /// of nothing, which is how every reprojection hears it.
        bool mPastLost;

        /// Null where the run is not being timed, which a picture is not.
        GpuTimer* mTimer = nullptr;
    };

    /// What one trace hands the display: the inputs every launch of it bound, the frame they
    /// compose, and the sprite tile list the trace read and what its tiles can meet, which the curve
    /// tests for where the puffs' composite drew nothing.
    struct TraceResult
    {
        VisibilityInputs mInputs;
        HandedImage mColour;

        /// An image of `mColour`'s shape and format that the display may lay the puffs over, so the
        /// composed frame stays the trace's own answer (`VulkanRenderer::readComposite`), with what
        /// it discards from at its first use: the chain's arena holds it over the display alone.
        HandedImage mCanvas;

        SpriteTables mSprites{};
    };
}
