#pragma once

#include <memory>

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/ref_ptr>

#include <components/rtx/renderer/frameimage.hpp>

namespace MyGUI
{
    class ITexture;
}

namespace MyGUIRtx
{
    class RenderManager;
}

namespace Rtx
{
    class Renderer;
}

namespace MWRender
{
    /// What the ray tracer reads back of the frame it last traced: a picture at a size — a screenshot, a save's
    /// thumbnail — and the frame a loading screen holds up. A read drains the queue, which a stop or a loading
    /// screen may pay and a frame may not, and reads into a buffer of its own that goes with the read: a frame
    /// is read rarely, and a buffer kept for it held 33 MB at 4K for the whole session.
    class FrameReader
    {
    public:
        /// The frame as `renderer` last traced it, as an image `width` by `height` — the frame's own size at
        /// nought — or null before anything was traced. Bottom row first, because every reader here takes
        /// OpenGL's order: `LoadingScreen` inverts V for it, and `osgDB`'s writers expect it.
        static osg::ref_ptr<osg::Image> read(Rtx::Renderer& renderer, int width, int height, Rtx::Channels channels);

        /// The frame frozen into the texture `gui` shows a loading screen through: the texture made on the first
        /// freeze and its image swapped on every one after, black where nothing was traced yet, which is what a
        /// fade from nothing looks like.
        MyGUI::ITexture& freeze(Rtx::Renderer& renderer, MyGUIRtx::RenderManager& gui);

    private:
        osg::ref_ptr<osg::Texture2D> mFrozenFrame;
        std::unique_ptr<MyGUI::ITexture> mFrozenFrameTexture;
    };
}
