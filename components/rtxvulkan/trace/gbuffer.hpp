#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/descriptorsets.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    class Device;

    /// What the two radiance channels, and the frame composed from them, are made of at a width:
    /// `Rtx::RadianceWidth` carries the argument for each.
    constexpr VkFormat radianceFormat(const RadianceWidth width)
    {
        return width == RadianceWidth::Summed ? toVulkanFormat(GBUFFER_RADIANCE_SUMMED)
                                              : toVulkanFormat(GBUFFER_RADIANCE_SHOWN);
    }

    /// What the trace leaves behind, before anything has decided what the picture looks like. A
    /// picture cannot be filtered and these can: one bounce per pixel is noisy, and the only thing
    /// that removes noise without removing detail is a blur over the light alone, so the light has
    /// to still be separate from the surface it landed on when the blur reaches it. By the time a
    /// pixel is a colour, the albedo is multiplied in, the fog is laid over it and the curve is
    /// applied, and nothing is left to filter that would not also smear the wall's texture. So the
    /// trace writes what it knows and the composite puts it back together, over the direct channel:
    ///
    ///     colour = direct + albedo * filter(indirect * transmittance)
    ///
    /// Where no filter stands between the two, the trace composes it there itself
    /// (`VisibilityConstants::mComposed`). Either way the direct channel is the frame afterwards.
    class GBuffer
    {
    public:
        /// @param radiance how wide the two radiance channels are stored, which is the run's choice
        ///        and `Rtx::RadianceWidth`'s argument.
        GBuffer(const Device& device, const SetLayout& layout, std::uint32_t width, std::uint32_t height,
            RadianceWidth radiance);

        /// The set every `GBuffer` is addressed through, made once and outliving all of them,
        /// because a pipeline layout names every set it will ever be handed, and the trace's
        /// pipelines are built before any camera has a size.
        static SetLayout describeLayout(const Device& device);

        /// One channel's image, which is the image bound at that channel's number.
        const Image& get(Channel channel) const { return mChannels[bindingOf(channel)]; }

        VkDescriptorSet getSet() const { return mSet.get(0); }

        std::uint32_t getWidth() const { return get(Channel::Direct).getWidth(); }
        std::uint32_t getHeight() const { return get(Channel::Direct).getHeight(); }

        /// Discards the contents and makes every channel writable, which is how a frame starts.
        /// Waits for the previous frame's readers to be done with them, so that one set of channels
        /// can serve a window that keeps several frames in flight.
        void begin(VkCommandBuffer commands) const;

        /// Orders the pass that wrote them against the passes about to read them, as
        /// `Use::sAnyShaderRead`. The composite, which writes the frame over the direct channel,
        /// orders its own write after this.
        void handOver(VkCommandBuffer commands) const;

    private:
        /// An array at each channel's binding (`bindingOf`), and not a member a channel. Named
        /// three times each — a member, an accessor, and a hand-written table mapping the binding
        /// back — a channel added to `Rtx::Channel` without the third reaches its pass as a null.
        std::vector<Image> mChannels;

        /// One set, in a pool of its own that goes with it.
        DescriptorSets mSet;
    };
}
