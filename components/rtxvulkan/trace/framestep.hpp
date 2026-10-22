#pragma once

#include <cstdint>

#include <components/rtxvulkan/device/memory/imagearena.hpp>

namespace Rtx
{
    /// The stretches of a frame's fixed order an image in a frame's arena is in use over
    /// (`ImageArena`): the trace's stages, the denoiser with the composite that reads its answers,
    /// the digest a read-back takes of them, the upscaler, and the display chain. The order
    /// `VulkanRenderer::renderFrame` records them in, which a picture keeps without the last three.
    enum class FrameStep : std::uint32_t
    {
        Trace,
        Denoise,
        Digest,
        Upscale,
        Display,
    };

    /// The span from `first` to `last`, both included.
    constexpr ArenaSpan spanOf(const FrameStep first, const FrameStep last)
    {
        return ArenaSpan{ .mFirst = static_cast<std::uint32_t>(first), .mLast = static_cast<std::uint32_t>(last) };
    }
}
