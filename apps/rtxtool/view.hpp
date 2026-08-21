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

        /// The cell, spelled the way `--cell` takes it: a pair of integers for an exterior, a name
        /// for an interior. Carried so the window can print a command line that comes back here.
        std::string mCell;
        std::filesystem::path mShaderDirectory;
        std::filesystem::path mScreenshotDirectory;

        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;
        float mFieldOfView = 60.0f;

        /// How much of the lighting painted into each texture to divide back out, from zero to one.
        /// Zero shows the textures as they were drawn, with their lighting still in them.
        float mDelight = 1.0f;

        /// Whether the denoiser runs. Off is what shows the noise it is there to remove.
        bool mFilter = true;

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

    /// One line of arguments that renders this frame again, wherever it is pasted.
    ///
    /// **What the window is looking at, plus everything that changes what it costs.** The camera and
    /// the size are passed rather than read off `request` because both move while the window is
    /// open; the rest of the conditions do not, and come off the request as they were given.
    ///
    /// The denoiser and the validation flags are in it deliberately, because both cost time a
    /// profiling line has to account for: five wavelet levels are about 2 ms at 1080p, and a trace
    /// timed under the layers is not a figure to compare against anything at all.
    std::string describeProfile(const ViewRequest& request, const Rtx::ValidationOptions& validation,
        const osg::Vec3f& origin, const osg::Vec3f& target, std::uint32_t width, std::uint32_t height);
}
