#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/descriptorsets.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>

#include "tracepast.hpp"

namespace Rtx
{
    class Device;

    /// What the radiance channels, and the frame composed from them, are made of at a width:
    /// `Rtx::RadianceWidth` carries the argument for each.
    constexpr VkFormat radianceFormat(const RadianceWidth width)
    {
        return width == RadianceWidth::Summed ? toVulkanFormat(GBUFFER_RADIANCE_SUMMED)
                                              : toVulkanFormat(GBUFFER_RADIANCE_SHOWN);
    }

    /// What a trace writes of the channels only some traces need, every other channel being written
    /// by every trace: the lobe's two where the scene places a material that wears a map (`HAS_MAPS`,
    /// `InstanceCounts::mMapped`), and the puffs' layer where a puff can be met
    /// (`VisibilityConstants::mPuffsInFrame`).
    struct ChannelWrites
    {
        bool mLobe = true;
        bool mPuffs = true;

        bool writes(Channel channel) const;
    };

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
        /// @param radiance how wide the radiance channels are stored, which is the run's choice
        ///        and `Rtx::RadianceWidth`'s argument.
        /// @param use what its channels are counted as: the frame's targets, or essential memory
        ///        where the chain grows while a frame reads the old one.
        /// @param past whether the frame before's surfaces are kept (`getHeld`): a second image of
        ///        each, or the frame's own where the past is dropped.
        GBuffer(const Device& device, const SetLayout& layout, std::uint32_t width, std::uint32_t height,
            RadianceWidth radiance, MemoryUse use, TracePast past);

        /// What a G-buffer of this extent and radiance width takes of the device's memory.
        static VkDeviceSize bytesAt(
            const Device& device, std::uint32_t width, std::uint32_t height, RadianceWidth radiance, TracePast past);

        /// The set every `GBuffer` is addressed through, made once and outliving all of them,
        /// because a pipeline layout names every set it will ever be handed, and the trace's
        /// pipelines are built before any camera has a size.
        static SetLayout describeLayout(const Device& device);

        /// One channel's image, which is the image bound at that channel's number.
        const Image& get(Channel channel) const;

        /// What `channel`, `Channel::Surface` or `Channel::PaneSurface`, held on the frame before:
        /// the surface every temporal filter holds its history to, **the trace's own channel and not
        /// a copy a filter made of it**, so a texel keeps the eye that saw it in its distance's sign
        /// and is rebuilt through that one (`heldSurfaceMatches`). This frame's own where the past is
        /// dropped, which a fresh history never reads.
        const Image& getHeld(Channel channel) const;

        VkDescriptorSet getSet() const { return mSet.get(mNow); }

        std::uint32_t getWidth() const { return get(Channel::Direct).getWidth(); }
        std::uint32_t getHeight() const { return get(Channel::Direct).getHeight(); }

        /// Discards the contents of every channel `writes` names and makes it writable, which is how a
        /// frame starts. Waits for the previous frame's readers to be done with them, so that one set
        /// of channels can serve a window that keeps several frames in flight.
        ///
        /// **A channel the trace does not write holds what the trace writes where it has nothing to
        /// say** — no lobe, no puff — so every reader meets the bits a trace that wrote it would have
        /// left: the digest, a read-back, and a pass that reads it regardless. Cleared on the trace
        /// that first leaves it out, and left alone by every trace after that leaves it out too.
        void begin(VkCommandBuffer commands, ChannelWrites writes);

        /// Orders the pass that wrote them against the passes about to read them, as
        /// `Use::sAnyShaderRead`. The composite, which writes the frame over the direct channel,
        /// orders its own write after this.
        void handOver(VkCommandBuffer commands) const;

    private:
        /// An array at each channel's index (`indexOf`), and not a member a channel. Named
        /// three times each — a member, an accessor, and a hand-written table mapping the index
        /// back — a channel added to `Rtx::Channel` without the third reaches its pass as a null.
        std::vector<Image> mChannels;

        /// The other image of each channel `getHeld` answers for, at its index among them: the one
        /// the frame before wrote, or the one this frame writes, by `mNow`. Empty where the past is
        /// dropped.
        std::array<Image, 2> mOthers;

        /// Which of a held channel's two images this frame writes, `mChannels`' or `mOthers`', and
        /// which set binds it: each frame's `begin` turns it.
        std::uint32_t mNow = 0;

        /// Whether the frame before's images are laid out, which the first `begin` does.
        bool mHeldLaidOut = false;

        /// Whether each channel holds its nothing from a clear `begin` recorded and nothing wrote
        /// it since. False from the start, since a channel made holds nothing defined.
        std::array<bool, sChannelCount> mHoldsNothing{};

        /// One set, in a pool of its own that goes with it.
        DescriptorSets mSet;
    };
}
