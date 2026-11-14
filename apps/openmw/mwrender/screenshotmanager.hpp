#pragma once

#include <osg/ref_ptr>

#include <osgViewer/Viewer>

namespace MWRender
{
    class NotifyDrawCompletedCallback;

    class ScreenshotManager
    {
    public:
        explicit ScreenshotManager(osgViewer::Viewer* viewer);
        ~ScreenshotManager();
        ScreenshotManager(const ScreenshotManager&) = delete;
        ScreenshotManager& operator=(const ScreenshotManager&) = delete;

        void screenshot(osg::Image* image, int w, int h);

    private:
        osg::ref_ptr<osgViewer::Viewer> mViewer;
        osg::ref_ptr<NotifyDrawCompletedCallback> mDrawCompleteCallback;
    };
}
