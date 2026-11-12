#include "rendererkeys.hpp"

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

    void RendererKeys::functionKey(const int index, const SDLUtil::KeyChange change)
    {
        // F3 and F4: the indices count from F1. A declined key is told once, as it goes down, and the renderer
        // hears none of it.
        if (index == 2 || index == 3)
            if (const std::string_view declined
                = mRenderer.support().declinedRequest(MWRender::PictureRequest::StatsOverlay);
                !declined.empty())
            {
                if (change == SDLUtil::KeyChange::Pressed)
                    MWBase::Environment::get().getWindowManager()->messageBox(
                        MWRender::notAvailable("The statistics overlay", declined));
                return;
            }
        mRenderer.functionKey(index, change);
    }

    void RendererKeys::windowResized(const int x, const int y, const int width, const int height)
    {
        mRenderer.windowResized(x, y, width, height);
    }
}
