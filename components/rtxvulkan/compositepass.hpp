#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/composite.h>

namespace Rtx
{
    class Device;
    class GBuffer;
    class Image;

    /// Puts the trace's channels back together and hands over a picture.
    ///
    /// **One multiply, one add and the curve** — everything harder was folded into the modulation
    /// term by the trace, which is what lets a filter sit in between knowing nothing about water,
    /// fog or tone. It also owns the running sum, because what a reference has to converge to is the
    /// picture as it will be shown.
    class CompositePass
    {
    public:
        CompositePass(const Device& device, const std::filesystem::path& shaderDirectory);
        ~CompositePass();

        CompositePass(const CompositePass&) = delete;
        CompositePass& operator=(const CompositePass&) = delete;

        /// @param buffer must have been handed over, so its writes are visible to this read.
        /// @param history a float image the size of `target`, in `VK_IMAGE_LAYOUT_GENERAL`, holding
        ///        the running sum when `mAccumulate` is set. Bound either way, and untouched when it
        ///        is not.
        /// @param target the displayable image, in `VK_IMAGE_LAYOUT_GENERAL`.
        void record(VkCommandBuffer commands, const GBuffer& buffer, const Image& history, const Image& target,
            const Shaders::CompositeConstants& constants) const;

    private:
        void build(const std::filesystem::path& shaderDirectory);
        void destroy();

        const Device& mDevice;
        VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        VkPipeline mPipeline = VK_NULL_HANDLE;
    };
}
