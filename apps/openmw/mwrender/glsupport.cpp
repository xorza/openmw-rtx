#include "glsupport.hpp"

#include <array>

namespace MWRender
{
    namespace
    {
        constexpr auto sSettings = std::to_array<SettingSupport>({
            { "RTX", "distant land cells",
                "The ray tracer's reach. The rasterizer's distance follows the viewing distance." },
            { "RTX", "specular map layout", "How the ray tracer reads a specular map's channels." },
            { "RTX", "upscale", "The ray tracer's upscaler. The rasterizer draws at the resolution." },
        });

        constexpr RenderSupport sSupport(sSettings, {}, {});
    }

    const RenderSupport& glSupport()
    {
        return sSupport;
    }
}
