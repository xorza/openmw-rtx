#include "screenshotmanager.hpp"

#include <condition_variable>
#include <mutex>

#include <components/crashcatcher/crash.hpp>
#include <components/misc/presentation.hpp>
#include <components/stereo/multiview.hpp>
#include <components/stereo/stereomanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

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
            std::lock_guard<std::mutex> lock(mMutex);
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

        void reset(unsigned int frame)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mDone = false;
            mFrame = frame;
        }

        mutable std::condition_variable mCondition;
        mutable std::mutex mMutex;
        mutable bool mDone;
        unsigned int mFrame;
    };

    class ReadImageFromFramebufferCallback : public osg::Drawable::DrawCallback
    {
    public:
        ReadImageFromFramebufferCallback(osg::Image* image, int width, int height)
            : mWidth(width)
            , mHeight(height)
            , mImage(image)
        {
        }
        void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* /*drawable*/) const override
        {
            const osg::Viewport* viewport
                = Crash::notNull(renderInfo.getCurrentCamera(), "a draw with no camera")->getViewport();
            int screenW = static_cast<int>(viewport->width());
            int screenH = static_cast<int>(viewport->height());
            if (Stereo::getStereo())
            {
                auto eyeRes = Stereo::Manager::instance().eyeResolution();
                screenW = eyeRes.x();
                screenH = eyeRes.y();
            }
            const Misc::Crop crop = Misc::cropToAspect(osg::Vec2i(screenW, screenH), osg::Vec2i(mWidth, mHeight));

            mImage->readPixels(
                crop.mOrigin.x(), crop.mOrigin.y(), crop.mSize.x(), crop.mSize.y(), GL_RGB, GL_UNSIGNED_BYTE);
            mImage->scaleImage(mWidth, mHeight, 1);
        }

    private:
        int mWidth;
        int mHeight;
        osg::ref_ptr<osg::Image> mImage;
    };

    ScreenshotManager::ScreenshotManager(osgViewer::Viewer* viewer)
        : mViewer(viewer)
        , mDrawCompleteCallback(new NotifyDrawCompletedCallback)
    {
    }

    ScreenshotManager::~ScreenshotManager() {}

    void ScreenshotManager::screenshot(osg::Image* image, int w, int h)
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
