#include "renderer.hpp"

#include <memory>

#include <components/crashcatcher/crash.hpp>

#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>

#include "glrenderer.hpp"

namespace MWRender
{
    // Its own file, so the seam's base compiles with neither renderer and neither includes the base
    // through the other.
    std::unique_ptr<Renderer> createRenderer(const RendererKind kind, const RendererSpec& spec)
    {
        switch (kind)
        {
            case RendererKind::OpenGl:
                return std::make_unique<GlRenderer>(spec);
            case RendererKind::RayTraced:
                return std::make_unique<RtxRenderer>(spec);
        }
        Crash::fatal("a renderer kind past the two");
    }
}
