#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/historyclamp.h>

namespace Rtx
{
    class Device;
    class Image;

    /// The glossy and the pane filters' anti-lag, `historyclamp.comp`: a filter's slow mean held to
    /// the box its fast means span around the pixel. One pipeline, which each filter records after
    /// its own dispatch over its own images.
    class HistoryClampPass
    {
    public:
        explicit HistoryClampPass(const Device& device);

        /// What one filter hands the clamp: the frame's samples of its light, the slow mean it just
        /// wrote, which is held in place, its blend of the fast mean, and the fast mean, which the
        /// filter read as last frame's and the clamp writes.
        struct Images
        {
            const Image& mSampled;
            const Image& mMean;
            const Image& mFastBlended;
            const Image& mFast;
        };

        /// Records the clamp over the frame `constants` names, behind the filter's writes, which this
        /// orders.
        void record(
            VkCommandBuffer commands, const Images& images, const Shaders::HistoryClampConstants& constants) const;

    private:
        ComputePipeline<Shaders::HistoryClampConstants> mPipeline;
    };
}
