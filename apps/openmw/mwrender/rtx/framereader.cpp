#include "framereader.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#include <MyGUI_ITexture.h>

#include <components/myguirtx/rendermanager.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/renderer/renderer.hpp>

namespace MWRender
{
    osg::ref_ptr<osg::Image> FrameReader::read(
        Rtx::Renderer& renderer, const int width, const int height, const Rtx::Channels channels)
    {
        const Rtx::FrameExtents extents = renderer.getExtents();
        if (extents.mOutputWidth == 0 || extents.mOutputHeight == 0)
            return nullptr;

        std::vector<std::uint8_t> pixels;
        renderer.readPixels(pixels);

        const Rtx::TracedFrame frame{
            .mWidth = extents.mOutputWidth,
            .mHeight = extents.mOutputHeight,
            .mPixels = pixels,
        };

        return Rtx::frameImage(frame, width > 0 ? width : static_cast<int>(frame.mWidth),
            height > 0 ? height : static_cast<int>(frame.mHeight), Rtx::RowOrder::BottomFirst, channels);
    }

    MyGUI::ITexture& FrameReader::freeze(Rtx::Renderer& renderer, MyGUIRtx::RenderManager& gui)
    {
        const osg::ref_ptr<osg::Image> taken = read(renderer, 0, 0, Rtx::Channels::Rgba);

        if (mFrozenFrame == nullptr)
            mFrozenFrame = new osg::Texture2D;

        if (taken != nullptr)
            mFrozenFrame->setImage(taken);

        if (mFrozenFrame->getImage() == nullptr)
        {
            osg::ref_ptr<osg::Image> black = new osg::Image;
            black->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
            std::memset(black->data(), 0, black->getTotalSizeInBytes());
            mFrozenFrame->setImage(black);
        }

        if (mFrozenFrameTexture == nullptr)
            mFrozenFrameTexture = gui.shareTexture(*mFrozenFrame);

        return *mFrozenFrameTexture;
    }
}
