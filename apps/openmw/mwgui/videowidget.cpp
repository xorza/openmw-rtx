#include "videowidget.hpp"

#include <cstdint>

#include <MyGUI_RenderManager.h>
#include <osg-ffmpeg-videoplayer/videoplayer.hpp>
#include <osg/Texture2D>

#include <apps/openmw/mwsound/movieaudiofactory.hpp>
#include <components/debug/debuglog.hpp>
#include <components/myguiplatform/guirendermanager.hpp>
#include <components/vfs/manager.hpp>

namespace MWGui
{

    VideoWidget::VideoWidget()
        : mVFS(nullptr)
    {
        mPlayer = std::make_unique<Video::VideoPlayer>();
        setNeedKeyFocus(true);
    }

    VideoWidget::~VideoWidget() = default;

    void VideoWidget::setVFS(const VFS::Manager* vfs)
    {
        mVFS = vfs;
    }

    void VideoWidget::playVideo(const std::string& video)
    {
        mPlayer->setAudioFactory(new MWSound::MovieAudioFactory());

        Files::IStreamPtr videoStream;
        try
        {
            videoStream = mVFS->get(video);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Failed to open video: " << e.what();
            return;
        }

        mPlayer->playVideo(std::move(videoStream), video);

        osg::ref_ptr<osg::Texture2D> texture = mPlayer->getVideoTexture();
        if (!texture)
            return;

        mTexture = MyGUIPlatform::shareTexture(*texture);

        setRenderItemTexture(mTexture.get());
        // Both the widget and the video frame are Y-down, so this UV is not inverted
        getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
    }

    std::int32_t VideoWidget::getVideoWidth()
    {
        return mPlayer->getVideoWidth();
    }

    std::int32_t VideoWidget::getVideoHeight()
    {
        return mPlayer->getVideoHeight();
    }

    bool VideoWidget::update()
    {
        return mPlayer->update();
    }

    void VideoWidget::commitFrame()
    {
        mPlayer->commitFrame();
    }

    void VideoWidget::stop()
    {
        mPlayer->close();
    }

    void VideoWidget::pause()
    {
        mPlayer->pause();
    }

    void VideoWidget::resume()
    {
        mPlayer->play();
    }

    bool VideoWidget::isPaused() const
    {
        return mPlayer->isPaused();
    }

    bool VideoWidget::hasAudioStream()
    {
        return mPlayer->hasAudioStream();
    }

    void VideoWidget::autoResize(bool stretch)
    {
        MyGUI::IntSize screenSize = MyGUI::RenderManager::getInstance().getViewSize();
        if (getParent())
            screenSize = getParent()->getSize();

        if (getVideoHeight() > 0 && !stretch)
        {
            double imageaspect = static_cast<double>(getVideoWidth()) / getVideoHeight();

            std::int32_t leftPadding
                = std::max(0, static_cast<std::int32_t>(screenSize.width - screenSize.height * imageaspect) / 2);
            std::int32_t topPadding
                = std::max(0, static_cast<std::int32_t>(screenSize.height - screenSize.width / imageaspect) / 2);

            setCoord(leftPadding, topPadding, screenSize.width - leftPadding * 2, screenSize.height - topPadding * 2);
        }
        else
            setCoord(0, 0, screenSize.width, screenSize.height);
    }

}
