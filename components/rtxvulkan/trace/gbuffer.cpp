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
        /// Half floats, and `gbuffer.h` has the angles the width is derived from. A normal is
        /// compared against a neighbour's and thrown away, never summed.
        constexpr VkFormat sGuide = toVulkanFormat(GBUFFER_GUIDE);

        /// Half floats, because an albedo is a fraction. The diffuse albedo takes it too, which
        /// needed measuring: quantising a per-pixel constant is a systematic error on the indirect
        /// term, but on a converged reference of a room the mean moves by a fiftieth of the
        /// tolerance the radiance channels are held to.
        constexpr VkFormat sAlbedo = toVulkanFormat(GBUFFER_ALBEDO);

        /// Two halves, for the reason `gbuffer.h` gives.
        constexpr VkFormat sMotion = toVulkanFormat(GBUFFER_MOTION);

        /// Two, and full floats rather than halves: a clip depth has little precision left at the
        /// far end of a Morrowind view, and the distance beside it runs past thirty thousand units
        /// where a half's steps are thirty-two units wide.
        constexpr VkFormat sDepth = toVulkanFormat(GBUFFER_DEPTH);

        /// Half floats for the layer the eye sees through: nothing sums it, and a reference is
        /// built with the upscaler off.
        constexpr VkFormat sLayer = toVulkanFormat(GBUFFER_LAYER);

        /// Four bytes for four fractions, which is what `gbuffer.h` argues a modulation is.
        constexpr VkFormat sBackdrop = toVulkanFormat(GBUFFER_BACKDROP);

        /// `SAMPLED` on all of them: an upscaler samples what it is handed, and the bit costs no
        /// memory, so every channel carries it rather than only the ones an upscaler reads.
        constexpr VkImageUsageFlags sUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        /// The channels a caller can ask to read back: the bounce, the albedo, the guide, the
        /// motion and the depth. See `Rtx::Channel`. And the direct channel, which
        /// is the frame once composed: `readComposite` copies it out, the frame a measurement is
        /// taken on, where `readPixels` gives the one a display would show.
        constexpr VkImageUsageFlags sReadable = sUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        struct ChannelFormat
        {
            VkFormat mFormat;
            VkImageUsageFlags mUsage;
        };

        /// What each channel is made of, at its own binding, placed by name so a channel added to
        /// `Rtx::Channel` and forgotten here is a compile error rather than an image bound at the
        /// wrong number. The two radiance channels take the run's width and the rest are fixed.
        ChannelFormat formatOf(const Channel channel, const RadianceWidth width)
        {
            static constexpr auto sFormats = [] {
                std::array<ChannelFormat, sChannelCount> every{};
                every[bindingOf(Channel::Direct)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[bindingOf(Channel::Indirect)] = { VK_FORMAT_UNDEFINED, sReadable };
                every[bindingOf(Channel::Albedo)] = { sAlbedo, sReadable };
                every[bindingOf(Channel::Guide)] = { sGuide, sReadable };
                every[bindingOf(Channel::Motion)] = { sMotion, sReadable };
                every[bindingOf(Channel::Depth)] = { sDepth, sReadable };
                every[bindingOf(Channel::Backdrop)] = { sBackdrop, sUsage };
                every[bindingOf(Channel::Puffs)] = { sLayer, sUsage };

                return every;
            }();

            static_assert(std::ranges::none_of(sFormats, [](const ChannelFormat& one) { return one.mUsage == 0; }),
                "a channel the format table did not fill");

            ChannelFormat described = sFormats[bindingOf(channel)];
            if (described.mFormat == VK_FORMAT_UNDEFINED)
                described.mFormat = radianceFormat(width);

            return described;
        }

        /// Every channel is a storage image the trace writes, bound one per number from nought,
        /// which is what `gbuffer.h`'s `CHANNEL_*` are. Both stages, because the trace is a launch
        /// and everything that reads what it left is a dispatch. One table serves the layout and the
        /// pool that holds a set of it.
        constexpr std::array<VkDescriptorSetLayoutBinding, sChannelCount> sBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, sChannelCount> bindings{};
            for (std::uint32_t channel = 0; channel < bindings.size(); ++channel)
                bindings[channel] = VkDescriptorSetLayoutBinding{ channel, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr };

            return bindings;
        }();
    }

    GBuffer::GBuffer(const Device& device, const SetLayout& layout, const std::uint32_t width,
        const std::uint32_t height, const RadianceWidth radiance)
        : mSet(device, sBindings, layout.get(), 1)
    {
        mChannels.reserve(sChannelCount);
        for (const Channel channel : sEveryChannel)
        {
            const ChannelFormat described = formatOf(channel, radiance);
            mChannels.emplace_back(device, width, height, described.mFormat, described.mUsage, channelName(channel));
        }

        DescriptorWrites<sChannelCount> writes(mSet.get(0));
        for (std::uint32_t channel = 0; channel < sChannelCount; ++channel)
            writes.image(channel, mChannels[channel].describeStorage());

        updateSets(device, writes.get());
    }

    void GBuffer::begin(VkCommandBuffer commands) const
    {
        // From undefined, because every pixel is written before any is read. One set of channels
        // serves every frame and two are in flight, and the head barrier `CommandPool::begin`
        // recorded is what orders this buffer after the last frame's readers.
        Barriers barriers(commands);
        for (const Image& image : mChannels)
            barriers.add(image.describeTransition(Use::sUndefined, Use::sTraceWrite));

        barriers.flush();
    }

    void GBuffer::handOver(VkCommandBuffer commands) const
    {
        // A read after a write, and nothing more: every channel is read-only from here to the end
        // of the frame but the direct one, which a composite writes the frame over and orders for
        // itself. Sampled as well as loaded, because an upscaler samples what it is handed.
        Barriers barriers(commands);
        for (const Image& image : mChannels)
            barriers.add(image.describeTransition(Use::sTraceWrite, Use::sAnyShaderRead));

        barriers.flush();
    }

    SetLayout GBuffer::describeLayout(const Device& device)
    {
        return makeSetLayout(device, sBindings);
    }
}
