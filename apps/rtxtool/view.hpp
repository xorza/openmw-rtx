#ifndef OPENMW_APPS_RTXTOOL_VIEW_H
#define OPENMW_APPS_RTXTOOL_VIEW_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <osg/Vec3f>

namespace Rtx
{
    struct InstanceOptions;
    class SceneDesc;
}

namespace RtxTool
{
    /// Everything the window needs that is not the world itself.
    struct ViewRequest
    {
        std::string mTitle;
        std::filesystem::path mShaderDirectory;
        std::filesystem::path mScreenshotDirectory;

        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;
        float mFieldOfView = 60.0f;

        std::optional<osg::Vec3f> mOrigin;
        std::optional<osg::Vec3f> mTarget;

        /// Close after this many frames. Zero waits for someone to close the window; anything else
        /// is how the window path gets exercised by something that cannot click.
        std::uint32_t mFrames = 0;
    };

    /// Opens a window on `scene` and flies around it until it is closed.
    int runWindow(const Rtx::SceneDesc& scene, const Rtx::InstanceOptions& instanceOptions, const ViewRequest& request);
}

#endif
