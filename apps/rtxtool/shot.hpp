#ifndef OPENMW_APPS_RTXTOOL_SHOT_H
#define OPENMW_APPS_RTXTOOL_SHOT_H

#include <cstdint>
#include <filesystem>
#include <optional>

#include <osg/Vec3f>

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    struct InstanceOptions;
    class SceneDesc;
}

namespace RtxTool
{
    /// Everything the screenshot needs that is not the world itself.
    struct ShotRequest
    {
        std::filesystem::path mOutput;
        std::filesystem::path mShaderDirectory;

        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;
        float mFieldOfView = 60.0f;

        /// Write the albedo with no shading over it.
        bool mShowAlbedo = false;

        /// Where to stand and what to look at. Both default to a view of the whole cell from outside
        /// it, which is the only placement that needs nothing known about the cell.
        std::optional<osg::Vec3f> mOrigin;
        std::optional<osg::Vec3f> mTarget;
    };

    /// Renders `scene` and writes a PNG. Returns a process exit status.
    ///
    /// Reports the fraction of primary rays that hit something, which is what tells "the cell
    /// rendered" from "the camera faced away from it" without anyone opening the file.
    int renderShot(const Rtx::SceneDesc& scene, Resource::ImageManager& images,
        const Rtx::InstanceOptions& instanceOptions, const ShotRequest& request);
}

#endif
