#pragma once

#include <cstdint>

#include <components/sdlutil/graphicslistener.hpp>

namespace MWRender
{
    class Renderer;
}

namespace MWInput
{
    /// **What the window's own keys reach**: the renderer, and where it declines what a key asks of it, the player
    /// told so by the game rather than by the renderer, which reaches no game manager. F3 and F4 ask for
    /// OpenSceneGraph's overlays (`PictureRequest::StatsOverlay`).
    class RendererKeys final : public SDLUtil::GraphicsListener
    {
    public:
        /// `renderer` outlives this.
        explicit RendererKeys(MWRender::Renderer& renderer);

        void beginEvents() override;
        void functionKey(std::int32_t index, SDLUtil::KeyChange change) override;
        void windowResized(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) override;

    private:
        MWRender::Renderer& mRenderer;
    };
}
