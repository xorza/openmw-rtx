#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <osg/Vec3f>

#include "lighting.hpp"

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    struct ValidationOptions;
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

        /// Write the albedo with no shading over it.
        bool mShowAlbedo = false;

        /// Filled in from the cell once it has been read, which is why both commands take their
        /// request by value.
        CellLighting mLighting;

        /// When and in what weather, for the exterior that has a sky. A weather is named as the
        /// fallback settings spell it, and the hour is on a twenty-four hour clock.
        std::string mWeather = "Clear";
        float mHour = 12.0f;

        std::optional<osg::Vec3f> mOrigin;
        std::optional<osg::Vec3f> mTarget;

        /// Close after this many frames. Zero waits for someone to close the window; anything else
        /// is how the window path gets exercised by something that cannot click.
        std::uint32_t mFrames = 0;
    };

    /// Opens a window on `scene` and flies around it until it is closed.
    int runWindow(const Rtx::SceneDesc& scene, Resource::ImageManager& images, const Rtx::ValidationOptions& validation,
        const ViewRequest& request);
}
