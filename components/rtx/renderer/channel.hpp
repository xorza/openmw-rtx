#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/shaders/gbuffer.h>

namespace Rtx
{
    /// One of the images the trace writes and everything after it reads, numbered by the
    /// shader's own `CHANNEL_*` from `shaders/gbuffer.h`, so a channel added to the shader without
    /// a case here is a build failure.
    enum class Channel : std::uint32_t
    {
        Direct = Shaders::CHANNEL_DIRECT,
        Indirect = Shaders::CHANNEL_INDIRECT,
        Albedo = Shaders::CHANNEL_ALBEDO,
        Surface = Shaders::CHANNEL_SURFACE,
        Motion = Shaders::CHANNEL_MOTION,
        Backdrop = Shaders::CHANNEL_BACKDROP,
        Puffs = Shaders::CHANNEL_PUFFS,
        Sunlit = Shaders::CHANNEL_SUNLIT,
        Specular = Shaders::CHANNEL_SPECULAR,
        Pane = Shaders::CHANNEL_PANE,
        PaneAlbedo = Shaders::CHANNEL_PANE_ALBEDO,
        PaneSurface = Shaders::CHANNEL_PANE_SURFACE,
        PaneMotion = Shaders::CHANNEL_PANE_MOTION,
        UpscaleMasks = Shaders::CHANNEL_UPSCALE_MASKS,
    };

    inline constexpr std::uint32_t sChannelCount = Shaders::CHANNEL_COUNT;

    inline constexpr std::uint32_t bindingOf(Channel channel)
    {
        return static_cast<std::uint32_t>(channel);
    }

    /// What a capture and a dump call each channel, and the one place they are written, in
    /// binding order, which is what `values()` then hands a walk that wants them all.
    inline constexpr NamedEnum<Channel, sChannelCount> sChannels{ { {
        { Channel::Direct, "g-direct" },
        { Channel::Indirect, "g-indirect" },
        { Channel::Albedo, "g-albedo" },
        { Channel::Surface, "g-surface" },
        { Channel::Motion, "g-motion" },
        { Channel::Backdrop, "g-backdrop" },
        { Channel::Puffs, "g-puffs" },
        { Channel::Sunlit, "g-sunlit" },
        { Channel::Specular, "g-specular" },
        { Channel::Pane, "g-pane" },
        { Channel::PaneAlbedo, "g-pane-albedo" },
        { Channel::PaneSurface, "g-pane-surface" },
        { Channel::PaneMotion, "g-pane-motion" },
        { Channel::UpscaleMasks, "g-upscale-masks" },
    } } };

    /// Every channel in binding order, for a walk that wants them all.
    inline constexpr std::array<Channel, sChannelCount> sEveryChannel = sChannels.values();
    static_assert(coversFromNought(sEveryChannel), "a channel the table leaves out, or names twice");

    inline constexpr std::string_view channelName(const Channel channel)
    {
        return sChannels.name(channel);
    }
}
