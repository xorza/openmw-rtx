#pragma once

#include <array>
#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/shadow.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "shadowhistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;

    /// The shadow denoiser: a port of AMD's FidelityFX Shadow Denoiser over the one bit a pixel's
    /// rays to the sun or a moon came back with, `CHANNEL_SUNLIT`'s alpha. A pass that packs the
    /// bits, a temporal pass that also classifies the tiles every receiver of which is lit alike,
    /// and three levels of a spatial filter over the rest — `shadowtiles.comp` and
    /// `shadowfilter.comp` say what the port keeps and what it changes. It runs where the wavelet
    /// does and the sky has a source that lights.
    class ShadowPass
    {
    public:
        ShadowPass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// What a frame hands the pass beside the images.
        struct Frame
        {
            /// The two eyes the trace cast rays through, as the wavelet takes them.
            Shaders::Camera mCamera;
            Shaders::Camera mArms;

            /// The accumulator's surface history for this frame, `AccumulateHistory::Turn`'s
            /// `mSurfaceBefore`, and the scale its distances are in: the same history, because it
            /// belongs to the same pixels of the same frames.
            const Image& mHeldSurface;
            float mDistanceScale;

            /// True where there is no history worth carrying, as the accumulator is told.
            bool mReset;
        };

        /// Records the five dispatches and hands back the filtered visibility, its mean in `r`,
        /// ordered for a compute read. `buffer` must have been handed over.
        const Image& record(
            VkCommandBuffer commands, const ShadowHistory::Turn& turn, const GBuffer& buffer, const Frame& frame) const;

    private:
        ComputePipeline mMask;
        ComputePipeline mTiles;
        std::array<ComputePipeline, Shaders::SHADOW_FILTER_LEVELS> mFilters;
    };
}
