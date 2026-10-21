#include "gbuffer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <tuple>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtxvulkan/device/bindingtable.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/traceconstants.h>

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
                every[indexOf(Channel::SpecularAlbedo)] = { sAlbedo, sReadable };
                every[indexOf(Channel::Lamped)] = { VK_FORMAT_UNDEFINED, sReadable };

                return every;
            }();

            static_assert(std::ranges::none_of(sFormats, [](const ChannelFormat& one) { return one.mUsage == 0; }),
                "a channel the format table did not fill");

            ChannelFormat described = sFormats[indexOf(channel)];
            if (described.mFormat == VK_FORMAT_UNDEFINED)
                described.mFormat = radianceFormat(width);

            // What `GBuffer::begin` clears a channel a trace may leave out through, read off the one
            // rule that says which those are.
            if (!ChannelWrites{ .mLobe = false, .mPuffs = false }.writes(channel))
                described.mUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            return described;
        }

        /// What a trace writes into a channel `ChannelWrites` may leave out, where it has nothing to
        /// say: no lobe — nought light at `SPECULAR_NO_LOBE`, and a specular albedo of one — and no
        /// puff, nought colour wholly let through.
        VkClearColorValue nothingOf(const Channel channel)
        {
            switch (channel)
            {
                case Channel::Specular:
                    return VkClearColorValue{ .float32 = { 0.0f, 0.0f, 0.0f, Shaders::SPECULAR_NO_LOBE } };
                case Channel::SpecularAlbedo:
                    return VkClearColorValue{ .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } };
                case Channel::Puffs:
                    return VkClearColorValue{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
                default:
                    break;
            }

            Crash::fatal("the nothing of a channel every trace writes");
        }

        /// The channels whose frame before every temporal filter reads (`GBuffer::getHeld`), at their
        /// index in `GBuffer::mOthers`.
        constexpr std::array sHeldChannels{ Channel::Surface, Channel::PaneSurface };

        /// `channel`'s index among `sHeldChannels`, or none.
        constexpr std::optional<std::size_t> heldIndexOf(const Channel channel)
        {
            for (std::size_t at = 0; at < sHeldChannels.size(); ++at)
                if (sHeldChannels[at] == channel)
                    return at;
            return std::nullopt;
        }

        ImageDescription descriptionOf(
            const Channel channel, const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
        {
            const ChannelFormat described = formatOf(channel, radiance);
            return ImageDescription{ .mWidth = channelWidth(channel, width),
                .mHeight = height,
                .mFormat = described.mFormat,
                .mUsage = described.mUsage };
        }

        /// The stage images, in fields as the trace writes them, and storage alone: only the
        /// trace's stages touch them.
        ImageDescription stageDescriptionOf(
            const Shaders::StorageFormat format, const std::uint32_t width, const std::uint32_t height)
        {
            return ImageDescription{ .mWidth = Shaders::storedWidth(width),
                .mHeight = height,
                .mFormat = toVulkanFormat(format),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT };
        }

        /// The bytes of the stage lists for a frame `width` by `height`: the head, a word a tile of
        /// each tile list (`stageTileCount`), and a word a pixel for the landing's.
        VkDeviceSize stageTilesBytes(const std::uint32_t width, const std::uint32_t height)
        {
            return VkDeviceSize{ Shaders::STAGE_TILES_HEAD_WORDS
                + Shaders::STAGE_LISTS * Shaders::stageTileCount(width, height) + width * height }
            * sizeof(std::uint32_t);
        }

        /// What the tile lists are made for: a storage buffer the stages append to, the indirect
        /// dispatch's arguments, and the head each frame writes inline.
        constexpr VkBufferUsageFlags sTileUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        /// Who reads a tile list: the stages, which append to it and read its tiles, and the indirect
        /// dispatch, which reads its head.
        constexpr BufferUse sTileReaders{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT };

        /// Every channel is a storage image the trace writes, and channel `c` binds at binding
        /// `indexOf(c)`, which is what `gbuffer.h`'s `CHANNEL_*` are; the stage images after them,
        /// at `STAGE_*`, and the tile lists, a storage buffer, last. Both stages, because the fog's
        /// and the sprites' launches share the stages' layout. One table serves the layout and the
        /// pool that holds a set of it.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::CHANNEL_SET_BINDINGS> sBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::CHANNEL_SET_BINDINGS> bindings{};
            for (std::uint32_t channel = 0; channel < bindings.size(); ++channel)
                bindings[channel] = VkDescriptorSetLayoutBinding{ channel,
                    channel == Shaders::STAGE_TILES ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                    : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr };

            return bindings;
        }();
    }

    GBuffer::GBuffer(const Device& device, const SetLayout& layout, const std::uint32_t width,
        const std::uint32_t height, const RadianceWidth radiance, const MemoryUse use, const TracePast past)
        : mStageRecord(use, device, stageDescriptionOf(GBUFFER_STAGE_RECORD, width, height), "stage record")
        , mStageThroughput(use, device, stageDescriptionOf(GBUFFER_STAGE_THROUGHPUT, width, height), "stage throughput")
        , mStageLift(use, device, stageDescriptionOf(GBUFFER_STAGE_LIFT, width, height), "stage lift")
        , mStageHit(use, device, stageDescriptionOf(GBUFFER_STAGE_HIT, width, height), "stage hit")
        , mStageRayFrom(use, device, stageDescriptionOf(GBUFFER_STAGE_RAY, width, height), "stage ray from")
        , mStageRayAlong(use, device, stageDescriptionOf(GBUFFER_STAGE_RAY, width, height), "stage ray along")
        , mStageBounce(use, device, stageDescriptionOf(GBUFFER_STAGE_BOUNCE, width, height), "stage bounce")
        , mStageTiles(Buffer::deviceLocal(device, stageTilesBytes(width, height), sTileUsage, "stage tiles"))
        , mSet(device, sBindings, layout.get(), past == TracePast::Kept ? 2 : 1)
    {
        static_assert(sHeldChannels.size() == std::tuple_size_v<decltype(mOthers)>, "a held channel with no image");

        mChannels.reserve(sChannelCount);
        for (const Channel channel : sEveryChannel)
            mChannels.emplace_back(use, device, descriptionOf(channel, width, height, radiance), channelName(channel));

        if (past == TracePast::Kept)
            for (std::size_t at = 0; at < sHeldChannels.size(); ++at)
                mOthers[at] = Image(use, device, descriptionOf(sHeldChannels[at], width, height, radiance),
                    channelName(sHeldChannels[at]));

        // A set a frame parity: the second binds each held channel's other image, and every other
        // channel as the first does.
        for (std::uint32_t set = 0; set < (past == TracePast::Kept ? 2u : 1u); ++set)
        {
            DescriptorWrites writes(layout, mSet.get(set));
            for (const Channel channel : sEveryChannel)
            {
                const std::optional<std::size_t> held = heldIndexOf(channel);
                const Image& bound = set == 1 && held.has_value() ? mOthers[*held] : mChannels[indexOf(channel)];
                writes.image(indexOf(channel), bound.describeStorage());
            }
            writes.image(Shaders::STAGE_RECORD, mStageRecord.describeStorage());
            writes.image(Shaders::STAGE_THROUGHPUT, mStageThroughput.describeStorage());
            writes.image(Shaders::STAGE_LIFT, mStageLift.describeStorage());
            writes.image(Shaders::STAGE_HIT, mStageHit.describeStorage());
            writes.image(Shaders::STAGE_RAY_FROM, mStageRayFrom.describeStorage());
            writes.image(Shaders::STAGE_RAY_ALONG, mStageRayAlong.describeStorage());
            writes.image(Shaders::STAGE_BOUNCE, mStageBounce.describeStorage());
            writes.buffer(Shaders::STAGE_TILES, mStageTiles.describe());
            updateSets(device, writes.get());
        }
    }

    VkDeviceSize GBuffer::bytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height,
        const RadianceWidth radiance, const TracePast past)
    {
        VkDeviceSize bytes = 0;
        for (const Channel channel : sEveryChannel)
            bytes += Image::bytesFor(device, descriptionOf(channel, width, height, radiance))
                * (past == TracePast::Kept && heldIndexOf(channel).has_value() ? 2 : 1);
        bytes += Image::bytesFor(device, stageDescriptionOf(GBUFFER_STAGE_RECORD, width, height));
        bytes += Image::bytesFor(device, stageDescriptionOf(GBUFFER_STAGE_THROUGHPUT, width, height));
        bytes += Image::bytesFor(device, stageDescriptionOf(GBUFFER_STAGE_LIFT, width, height));
        bytes += Image::bytesFor(device, stageDescriptionOf(GBUFFER_STAGE_HIT, width, height));
        bytes += 2 * Image::bytesFor(device, stageDescriptionOf(GBUFFER_STAGE_RAY, width, height));
        bytes += Image::bytesFor(device, stageDescriptionOf(GBUFFER_STAGE_BOUNCE, width, height));
        return bytes;
    }

    VkDeviceSize GBuffer::tileBytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        return Buffer::bytesFor(device, stageTilesBytes(width, height), sTileUsage);
    }

    const Image& GBuffer::get(const Channel channel) const
    {
        const std::optional<std::size_t> held = heldIndexOf(channel);
        return held.has_value() && mNow == 1 ? mOthers[*held] : mChannels[indexOf(channel)];
    }

    const Image& GBuffer::getHeld(const Channel channel) const
    {
        const std::optional<std::size_t> held = heldIndexOf(channel);
        assert(held.has_value() && "the frame before of a channel no temporal filter reads");
        if (mOthers[*held].isEmpty())
            return mChannels[indexOf(channel)];
        return mNow == 1 ? mChannels[indexOf(channel)] : mOthers[*held];
    }

    // One command a hand-over and not two: a run past the batch's room emits what it holds.
    static_assert(
        Shaders::CHANNEL_SET_BINDINGS <= Barriers::sMostImages, "the G-buffer's channels overflow one barrier batch");
    static_assert(Shaders::CHANNEL_SET_BINDINGS <= BindingTable::sMost, "the G-buffer's set overflows a binding table");

    bool ChannelWrites::writes(const Channel channel) const
    {
        switch (channel)
        {
            case Channel::Specular:
            case Channel::SpecularAlbedo:
                return mLobe;
            case Channel::Puffs:
                return mPuffs;
            default:
                return true;
        }
    }

    void GBuffer::begin(VkCommandBuffer commands, const ChannelWrites writes)
    {
        // The frame before's surfaces become what this one holds them to, and the images it read
        // as those are this frame's to write.
        const bool keeps = !mOthers.front().isEmpty();
        if (keeps)
            mNow ^= 1u;

        // From undefined, because every pixel is written before any is read. One set of channels
        // serves every frame and two are in flight, and the head barrier `CommandPool::begin`
        // recorded is what orders this buffer after the last frame's readers.
        Barriers barriers(commands);
        for (const Channel channel : sEveryChannel)
            if (writes.writes(channel))
            {
                get(channel).addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
                mHoldsNothing[indexOf(channel)] = false;
            }
        mStageRecord.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        mStageThroughput.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        mStageLift.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        mStageHit.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        mStageRayFrom.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        mStageRayAlong.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        mStageBounce.addTransition(barriers, Use::sUndefined, Use::sComputeWrite);

        // **The frame before's laid out where no frame wrote it**, the first: a history that fresh
        // reads none of it, and a binding states it all the same.
        if (keeps && !mHeldLaidOut)
            for (const Channel channel : sHeldChannels)
                getHeld(channel).addTransition(barriers, Use::sUndefined, Use::sComputeRead);
        mHeldLaidOut = true;

        barriers.flush();

        // **The lists start empty**: each list's dispatch at no group across, one down and one deep,
        // which the trace and the bounce count up as they append, and no pixel landed.
        constexpr std::array<std::uint32_t, Shaders::STAGE_TILES_HEAD_WORDS> sEmpty{ 0u, 1u, 1u, 0u, 0u, 1u, 1u, 0u, 0u,
            1u, 1u, 0u };
        mStageTiles.updateInline(commands, sTileReaders, std::as_bytes(std::span(sEmpty)));

        // Left as a write of the trace's, so `handOver` orders it for the readers as it orders what
        // the trace wrote. Rare: a scene that wears no map, or a frame no puff is met in, is so for
        // every frame until it changes.
        for (const Channel channel : sEveryChannel)
            if (!writes.writes(channel) && !mHoldsNothing[indexOf(channel)])
            {
                get(channel).clear(commands, Use::sUndefined, nothingOf(channel), Use::sComputeWrite);
                mHoldsNothing[indexOf(channel)] = true;
            }
    }

    void GBuffer::handToStage(VkCommandBuffer commands)
    {
        Barriers barriers(commands);
        barriers.add(VkMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
        });
        barriers.flush();
    }

    void GBuffer::handOver(VkCommandBuffer commands) const
    {
        // A read after a write, and nothing more: every channel is read-only from here to the end
        // of the frame but the direct one, which a composite writes the frame over and orders for
        // itself. Sampled as well as loaded, because an upscaler samples what it is handed.
        Barriers barriers(commands);
        for (const Channel channel : sEveryChannel)
            get(channel).addTransition(barriers, Use::sComputeWrite, Use::sAnyShaderRead);

        barriers.flush();
    }

    SetLayout GBuffer::describeLayout(const Device& device)
    {
        return makeSetLayout(device, sBindings);
    }
}
