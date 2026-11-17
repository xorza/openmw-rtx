#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <osg/ref_ptr>
#include <osgViewer/ViewerEventHandlers>

#include <components/misc/guarded.hpp>

namespace osg
{
    class Image;
}

namespace SceneUtil
{
    class WorkQueue;
    class WorkItem;

    std::filesystem::path writeScreenshotToFile(
        const std::filesystem::path& screenshotPath, const std::string& screenshotFormat, const osg::Image& image);

    class WriteScreenshotToFileOperation : public osgViewer::ScreenCaptureHandler::CaptureOperation
    {
    public:
        WriteScreenshotToFileOperation(std::filesystem::path screenshotPath, std::string screenshotFormat,
            std::function<void(std::string)> callback);

        void operator()(const osg::Image& image, std::uint32_t contextId) override;

    private:
        const std::filesystem::path mScreenshotPath;
        const std::string mScreenshotFormat;
        const std::function<void(std::string)> mCallback;
    };

    class AsyncScreenCaptureOperation : public osgViewer::ScreenCaptureHandler::CaptureOperation
    {
    public:
        AsyncScreenCaptureOperation(std::shared_ptr<SceneUtil::WorkQueue> queue,
            osg::ref_ptr<osgViewer::ScreenCaptureHandler::CaptureOperation> impl);

        ~AsyncScreenCaptureOperation() override;
        AsyncScreenCaptureOperation(const AsyncScreenCaptureOperation&) = delete;
        AsyncScreenCaptureOperation& operator=(const AsyncScreenCaptureOperation&) = delete;

        void stop();

        void operator()(const osg::Image& image, std::uint32_t contextId) override;

    private:
        const std::shared_ptr<SceneUtil::WorkQueue> mQueue;
        const osg::ref_ptr<osgViewer::ScreenCaptureHandler::CaptureOperation> mImpl;
        Misc::ScopeGuarded<std::vector<std::shared_ptr<SceneUtil::WorkItem>>> mWorkItems;
    };
}
