#include "rendererkeys.hpp"

#include <cstdint>
#include <format>
#include <string_view>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwrender/renderer.hpp>
#include <apps/openmw/mwrender/rendersupport.hpp>

namespace MWInput
{
    RendererKeys::RendererKeys(MWRender::Renderer& renderer)
        : mRenderer(renderer)
    {
    }

    void RendererKeys::beginEvents()
    {
        mRenderer.beginEvents();
    }

    void RendererKeys::functionKey(const std::int32_t index, const SDLUtil::KeyChange change)
    {
        // A declined key is told once, as it goes down, by its name and the renderer's reason: the renderer knows what
        // the key asks, and the indices count from F1.
        const std::string_view declined = mRenderer.functionKey(index, change);
        if (declined.empty() || change != SDLUtil::KeyChange::Pressed)
            return;
        MWBase::Environment::get().getWindowManager()->messageBox(
            MWRender::notAvailable(std::format("F{}", index + 1), declined));
    }

    void RendererKeys::windowResized(
        const std::int32_t x, const std::int32_t y, const std::int32_t width, const std::int32_t height)
    {
        mRenderer.windowResized(x, y, width, height);
    }
}
