#include "gbuffer.hpp"

#include <algorithm>
#include <array>

#include <components/rtx/shaders/gbuffer.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// Half floats, because an albedo is a fraction. The diffuse albedos take it too, which
        /// needed measuring: quantising a per-pixel constant is a systematic error on the indirect
        /// term, but on a converged reference of a room the mean moves by a fiftieth of the
        /// tolerance the radiance channels are held to.
        constexpr VkFormat sAlbedo = toVulkanFormat(GBUFFER_ALBEDO);

        /// Two halves, for the reason `gbuffer.h` gives.
        constexpr VkFormat sMotion = toVulkanFormat(GBUFFER_MOTION);

        /// Two full floats, for the reasons `gbuffer.h` gives: the distance runs past thirty thousand
        /// units, where a half's steps are thirty-two units wide.
        constexpr VkFormat sSurface = toVulkanFormat(GBUFFER_SURFACE);

        /// Half floats for the layer the eye sees through: nothing sums it, and a reference is
        /// built with the upscaler off.
        constexpr VkFormat sLayer = toVulkanFormat(GBUFFER_LAYER);

        /// Four bytes for four fractions, which is what `gbuffer.h` argues a modulation is.
        constexpr VkFormat sBackdrop = toVulkanFormat(GBUFFER_BACKDROP);

        /// A byte a mask, the width AMD stores its own at.
        constexpr VkFormat sUpscaleMasks = toVulkanFormat(GBUFFER_UPSCALE_MASKS);

        /// A byte a channel, for the reason `gbuffer.h` gives.
        constexpr VkFormat sLift = toVulkanFormat(GBUFFER_LIFT);

        /// A half, which holds a radius in pixels finely enough to gate a filter level by, and
        /// `SHADOW_PENUMBRA_CLEAR`.
        constexpr VkFormat sPenumbra = toVulkanFormat(GBUFFER_PENUMBRA);

        /// `SAMPLED` on all of them: the cascade samples the surface and the puffs, an upscaler samples
        /// what it is handed, and the bit costs no memory, so every channel carries it.
        constexpr VkImageUsageFlags sUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        /// The channels a caller can ask to read back: every one but the backdrop and the puffs. See
        /// `Rtx::Channel`. The direct channel among them is the frame once composed: `readComposite`
        /// copies it out, the frame a measurement is taken on, where `readPixels` gives the one a
        /// display would show.
        constexpr VkImageUsageFlags sReadable = sUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        struct ChannelFormat
        {
            VkFormat mFormat;
            VkImageUsageFlags mUsage;
        };

        /// What each channel is made of, at its own index, placed by name so a channel added to
        /// `Rtx::Channel` and forgotten here is a compile error rather than an image bound at the
        /// wrong number. The radiance channels take the run's width and the rest are fixed.
        ChannelFormat formatOf(const Channel channel, const RadianceWidth width)
        {
            static constexpr auto sFormats = [] {
                std::array<ChannelFormat, sChannelCount> every{};
                every[indexOf(Channel::Direct)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::Indirect)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::Albedo)] = { sAlbedo, sReadable };
                every[indexOf(Channel::Surface)] = { sSurface, sReadable };
                every[indexOf(Channel::Motion)] = { sMotion, sReadable };
                every[indexOf(Channel::Backdrop)] = { sBackdrop, sUsage };
                every[indexOf(Channel::Puffs)] = { sLayer, sUsage };
                every[indexOf(Channel::Shadowed)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::Specular)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::Pane)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::PaneAlbedo)] = { sAlbedo, sReadable };
                every[indexOf(Channel::PaneSurface)] = { sSurface, sReadable };
                every[indexOf(Channel::PaneMotion)] = { sMotion, sReadable };
                every[indexOf(Channel::UpscaleMasks)] = { sUpscaleMasks, sReadable };
                every[indexOf(Channel::Fill)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::AmbientAlbedo)] = { sAlbedo, sReadable };
                every[indexOf(Channel::Lift)] = { sLift, sReadable };
                every[indexOf(Channel::Penumbra)] = { sPenumbra, sReadable };
                every[indexOf(Channel::SpecularAlbedo)] = { sAlbedo, sReadable };
                every[indexOf(Channel::Lamped)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[indexOf(Channel::LampPenumbra)] = { sPenumbra, sReadable };

                return every;
            }();

            static_assert(std::ranges::none_of(sFormats, [](const ChannelFormat& one) { return one.mUsage == 0; }),
                "a channel the format table did not fill");

            ChannelFormat described = sFormats[indexOf(channel)];
            if (described.mFormat == VK_FORMAT_UNDEFINED)
                described.mFormat = radianceFormat(width);

            return described;
        }

        ImageDescription descriptionOf(
            const Channel channel, const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
        {
            const ChannelFormat described = formatOf(channel, radiance);
            return ImageDescription{
                .mWidth = width, .mHeight = height, .mFormat = described.mFormat, .mUsage = described.mUsage
            };
        }

        /// Every channel is a storage image the trace writes, and channel `c` binds at binding
        /// `indexOf(c)`, which is what `gbuffer.h`'s `CHANNEL_*` are. Both stages, because the trace
        /// is a launch and everything that reads what it left is a dispatch. One table serves the
        /// layout and the pool that holds a set of it.
        constexpr std::array<VkDescriptorSetLayoutBinding, sChannelCount> sBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, sChannelCount> bindings{};
            for (std::uint32_t channel = 0; channel < bindings.size(); ++channel)
                bindings[channel] = VkDescriptorSetLayoutBinding{ channel, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr };

            return bindings;
        }();
    }

    GBuffer::GBuffer(const Device& device, const SetLayout& layout, const std::uint32_t width,
        const std::uint32_t height, const RadianceWidth radiance, const MemoryUse use)
        : mSet(device, sBindings, layout.get(), 1)
    {
        mChannels.reserve(sChannelCount);
        for (const Channel channel : sEveryChannel)
            mChannels.emplace_back(use, device, descriptionOf(channel, width, height, radiance), channelName(channel));

        DescriptorWrites writes(layout, mSet.get(0));
        for (std::uint32_t channel = 0; channel < sChannelCount; ++channel)
            writes.image(channel, mChannels[channel].describeStorage());

        updateSets(device, writes.get());
    }

    VkDeviceSize GBuffer::bytesAt(
        const Device& device, const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
    {
        VkDeviceSize bytes = 0;
        for (const Channel channel : sEveryChannel)
            bytes += Image::bytesFor(device, descriptionOf(channel, width, height, radiance));
        return bytes;
    }

    // One command a hand-over and not two: a run past the batch's room emits what it holds.
    static_assert(
        Shaders::CHANNEL_COUNT <= Barriers::sMostImages, "the G-buffer's channels overflow one barrier batch");

    void GBuffer::begin(VkCommandBuffer commands) const
    {
        // From undefined, because every pixel is written before any is read. One set of channels
        // serves every frame and two are in flight, and the head barrier `CommandPool::begin`
        // recorded is what orders this buffer after the last frame's readers.
        Barriers barriers(commands);
        for (const Image& image : mChannels)
            image.addTransition(barriers, Use::sUndefined, Use::sTraceWrite);

        barriers.flush();
    }

    void GBuffer::handOver(VkCommandBuffer commands) const
    {
        // A read after a write, and nothing more: every channel is read-only from here to the end
        // of the frame but the direct one, which a composite writes the frame over and orders for
        // itself. Sampled as well as loaded, because an upscaler samples what it is handed.
        Barriers barriers(commands);
        for (const Image& image : mChannels)
            image.addTransition(barriers, Use::sTraceWrite, Use::sAnyShaderRead);

        barriers.flush();
    }

    SetLayout GBuffer::describeLayout(const Device& device)
    {
        return makeSetLayout(device, sBindings);
    }
}
