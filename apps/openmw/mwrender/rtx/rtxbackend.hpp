#pragma once

#include <array>
#include <memory>
#include <string_view>
#include <utility>

#include <SDL3/SDL_video.h>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    class Renderer;
    struct RendererOptions;
}

namespace MWRender
{
    /// What draws the frames: the device, through the Vulkan backend, or nothing at all
    /// (`Rtx::NullRenderer`), for a run of the game on a machine with no device. The one place the
    /// game tells the backends apart: what each window is made with, and what each is made by.
    enum class RtxBackend
    {
        Vulkan,
        Null,
    };

    /// How a backend is spelled where a run names one: `OPENMW_RTX_BACKEND`.
    inline constexpr Rtx::NamedEnum sRtxBackendNames{ std::array{
        std::pair{ RtxBackend::Vulkan, std::string_view("vulkan") },
        std::pair{ RtxBackend::Null, std::string_view("null") },
    } };

    /// What a window `backend` presents to is made with: the Vulkan backend's surface, which throws
    /// `Rtx::Unsupported` where no driver presents to one, or none for the null backend, which
    /// presents nothing. SDL's offscreen driver makes a window with none where there is no display.
    SDL_WindowFlags surfaceFlagsOf(RtxBackend backend);

    /// The backend itself, over the window `options` names.
    std::unique_ptr<Rtx::Renderer> createBackend(RtxBackend backend, const Rtx::RendererOptions& options);
}
