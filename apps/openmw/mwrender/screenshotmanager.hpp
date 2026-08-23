#ifndef MWRENDER_SCREENSHOTMANAGER_H
#define MWRENDER_SCREENSHOTMANAGER_H

#include <osg/ref_ptr>

namespace osg
{
    class Image;
}

namespace MWRender
{
    class NotifyDrawCompletedCallback;
    class Stage;

    class ScreenshotManager
    {
    public:
        explicit ScreenshotManager(Stage& stage);
        ~ScreenshotManager();

        void screenshot(osg::Image* image, int w, int h);

    private:
        Stage& mStage;
        osg::ref_ptr<NotifyDrawCompletedCallback> mDrawCompleteCallback;
    };
}

#endif
