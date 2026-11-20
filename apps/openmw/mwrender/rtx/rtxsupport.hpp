#pragma once

#include <string_view>

#include <apps/openmw/mwrender/rendersupport.hpp>

namespace MWRender
{
    /// What the ray tracer honours, and why it declines the rest.
    const RenderSupport& rtxSupport();

    /// Why the ray tracer reloads no shader, at once or as it changes (`Renderer::reloadShaders`).
    inline constexpr std::string_view sKernelsCompiledIn
        = "The ray tracer's kernels are compiled into the build, and a rebuild changes them.";

    /// Why the ray tracer takes no F3 or F4 (`Renderer::functionKey`).
    inline constexpr std::string_view sNoStatsOverlays
        = "The profiler and the statistics are OpenSceneGraph's overlays, which the rasterizer draws. The ray "
          "tracer's frame rate and frame time are in the window's title.";
}
