#pragma once

#include <optional>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/atrous.h>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;
    class GpuTimer;

    /// The denoiser: a few edge-stopping wavelet levels over the indirect channel and its fill,
    /// borrowing samples sideways from neighbours on the same surface because there is no time for
    /// enough bounces per pixel. Legitimate because the trace demodulated: this filters light, and
    /// the texture is multiplied back in afterwards. The sky, water and fog were resolved into
    /// `direct` and pass this by.
    class AtrousPass
    {
    public:
        explicit AtrousPass(const Device& device);

        /// Where the last level left the bounce and its fill.
        struct Filtered
        {
            const Image& mIndirect;
            const Image& mFill;
        };

        /// What the last level composes the frame from where it composes (`atrouscompose.comp`),
        /// beside the channels and its own answer: the other filters' answers — the lobe's, the
        /// layers', and each shadow field's, or null where the field did not run — and whether a
        /// surface can have a lobe (`CompositeConstants::mLobed`).
        struct Composing
        {
            const Image& mSpecular;
            const Image& mPane;
            const Image* mSkyShadow;
            const Image* mLampShadow;
            bool mLobed;
        };

        /// Runs every level and returns the images the result ended up in, because the levels
        /// alternate and a copy back would be bandwidth spent on tidiness.
        ///
        /// @param images the accumulator's: its blends of this frame's bounce and fill, with the
        ///        bounce's variance that turns a difference in brightness into an edge or into noise,
        ///        are the first level's input; the colour and fill histories are what the first level
        ///        writes and the accumulator finds as its means next frame, SVGF's feedback; the
        ///        scratches are the other half of the ping-pong.
        /// @param buffer handed over, so the trace's writes are visible. Only the surface is read
        ///        from it.
        /// @param frame whose two eyes the edge tests rebuild the trace's rays through.
        /// @param composing where the last level composes the frame over `CHANNEL_DIRECT`, which it
        ///        reads and writes: the caller has ordered it for both, and orders it for what reads it
        ///        next. Null where the composite does, a frame that sums.
        /// @return the last level's answer, or nothing where it composed the frame instead.
        std::optional<Filtered> record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame, const Composing* composing, GpuTimer* timer) const;

    private:
        /// The first level, wide, and every level after it, narrow (`ATROUS_WIDE`).
        ComputePipeline<Shaders::AtrousConstants> mWide;
        ComputePipeline<Shaders::AtrousConstants> mNarrow;

        /// The last level where it composes the frame, a narrow level with the composite's channels.
        ComputePipeline<Shaders::AtrousConstants> mComposing;

        /// What a shadow field that did not run is bound as, which the composing level never reads.
        Image mNoShadow;
    };
}
