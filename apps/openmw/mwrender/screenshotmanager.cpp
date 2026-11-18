#include "screenshotmanager.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <components/stereo/stereomanager.hpp>

#include "postprocessor.hpp"

namespace MWRender
{

    class NotifyDrawCompletedCallback : public osg::Camera::DrawCallback
    {
    public:
        NotifyDrawCompletedCallback()
            : mDone(false)
            , mFrame(0)
        {
        }

        void operator()(osg::RenderInfo& renderInfo) const override
        {
            std::scoped_lock lock(mMutex);
            if (renderInfo.getState()->getFrameStamp()->getFrameNumber() >= mFrame && !mDone)
            {
                mDone = true;
                mCondition.notify_one();
            }
        }

        void waitTillDone()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            if (mDone)
                return;
            mCondition.wait(lock);
        }

        void reset(std::uint32_t frame)
        {
            std::scoped_lock lock(mMutex);
            mDone = false;
            mFrame = frame;
        }

        mutable std::condition_variable mCondition;
        mutable std::mutex mMutex;
        mutable bool mDone;
        std::uint32_t mFrame;
    };

    namespace
    {
        class ReadImageFromFramebufferCallback : public osg::Drawable::DrawCallback
        {
        public:
            ReadImageFromFramebufferCallback(osg::Image* image, std::int32_t width, std::int32_t height)
                : mWidth(width)
                , mHeight(height)
                , mImage(image)
            {
            }
            void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* /*drawable*/) const override
            {
                auto screenW = static_cast<std::int32_t>(renderInfo.getCurrentCamera()->getViewport()->width());
                auto screenH = static_cast<std::int32_t>(renderInfo.getCurrentCamera()->getViewport()->height());
                if (Stereo::getStereo())
                {
                    auto eyeRes = Stereo::Manager::instance().eyeResolution();
                    screenW = eyeRes.x();
                    screenH = eyeRes.y();
                }
                double imageaspect = static_cast<double>(mWidth) / static_cast<double>(mHeight);
                std::int32_t leftPadding = std::max(0, static_cast<std::int32_t>(screenW - screenH * imageaspect) / 2);
                std::int32_t topPadding = std::max(0, static_cast<std::int32_t>(screenH - screenW / imageaspect) / 2);
                std::int32_t width = screenW - leftPadding * 2;
                std::int32_t height = screenH - topPadding * 2;

                mImage->readPixels(leftPadding, topPadding, width, height, GL_RGB, GL_UNSIGNED_BYTE);
                mImage->scaleImage(mWidth, mHeight, 1);
            }

        private:
            std::int32_t mWidth;
            std::int32_t mHeight;
            osg::ref_ptr<osg::Image> mImage;
        };
    }

    ScreenshotManager::ScreenshotManager(osgViewer::Viewer* viewer)
        : mViewer(viewer)
        , mDrawCompleteCallback(new NotifyDrawCompletedCallback)
    {
    }

    ScreenshotManager::~ScreenshotManager() {}

    void ScreenshotManager::screenshot(osg::Image* image, std::int32_t w, std::int32_t h)
    {
        osg::Camera* camera = MWBase::Environment::get().getWorld()->getPostProcessor()->getHUDCamera();
        osg::ref_ptr<osg::Drawable> tempDrw = new osg::Drawable;
        tempDrw->setDrawCallback(new ReadImageFromFramebufferCallback(image, w, h));
        tempDrw->setCullingActive(false);
        tempDrw->getOrCreateStateSet()->setRenderBinDetails(100, "RenderBin",
            osg::StateSet::USE_RENDERBIN_DETAILS); // so its after all scene bins but before POST_RENDER gui camera
        camera->addChild(tempDrw);

        // Ref https://gitlab.com/OpenMW/openmw/-/issues/6013
        mDrawCompleteCallback->reset(mViewer->getFrameStamp()->getFrameNumber());
        mViewer->getCamera()->setFinalDrawCallback(mDrawCompleteCallback);
        mViewer->eventTraversal();
        mViewer->updateTraversal();
        mViewer->renderingTraversals();
        mDrawCompleteCallback->waitTillDone();

        // now that we've "used up" the current frame, get a fresh frame number for the next frame() following after the
        // screenshot is completed
        mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());
        camera->removeChild(tempDrw);
    }
}
