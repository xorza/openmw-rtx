#pragma once

#include <cstdint>
#include <limits>
#include <memory>

#include <SDL_video.h>

namespace osg
{
    class Camera;
}

namespace Rtx
{
    class Renderer;
}

namespace MWRender
{
    /// The SDL window the ray tracer draws into: made here, fitted to as it changes size, and
    /// written to for the one thing this renderer says about itself, its speed. No GL context is
    /// ever made on it, which is the point of the whole path. Before the backend in whatever owns
    /// both, because the backend's surface is on it.
    class RtxWindow
    {
    public:
        /// Makes the window from the video settings; `hidden` is a headless run, the same window
        /// with nobody watching. Throws where SDL refuses.
        explicit RtxWindow(bool hidden);

        SDL_Window* get() const { return mWindow.get(); }

        /// The size the window last reported, in pixels, at least one by one.
        std::uint32_t getWidth() const { return mAskedWidth; }
        std::uint32_t getHeight() const { return mAskedHeight; }

        /// Sizes the trace, the surface and the viewport to the window once its size has settled,
        /// `now` being the host's clock (`Misc::FrameClock::getNow`). Asked every frame, because a
        /// Wayland surface has no size of its own — its `currentExtent` is `0xFFFFFFFF` by
        /// specification — so a present succeeds for ever and the compositor stretches the picture
        /// to whatever the window became.
        void fit(Rtx::Renderer& renderer, osg::Camera& camera, double now);

        /// Sizes the trace, the surface and the viewport to the size the window last reported, now:
        /// what `fit` does once the size settles, and what a renderer just made does before any
        /// frame has a clock.
        void apply(Rtx::Renderer& renderer, osg::Camera& camera) const;

        /// Writes the title, where somebody can see it: a hidden window keeps whatever it had.
        void setTitle(const char* title);

    private:
        /// The size SDL reports now, at least one by one.
        void readSize(std::uint32_t& width, std::uint32_t& height) const;

        std::unique_ptr<SDL_Window, void (*)(SDL_Window*)> mWindow{ nullptr, SDL_DestroyWindow };

        /// The size the window last reported and when it first reported it — not the extent
        /// anything is drawn at, which `Rtx::FrameExtents` says. Never is further back than any
        /// moment, which is what makes the first fit act rather than wait.
        std::uint32_t mAskedWidth = 0;
        std::uint32_t mAskedHeight = 0;
        double mAskedSince = -std::numeric_limits<double>::infinity();
    };
}
