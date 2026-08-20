#ifndef OPENMW_APPS_RTXTOOL_SHOT_H
#define OPENMW_APPS_RTXTOOL_SHOT_H

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

        /// How many times to trace the same frame before reporting on it.
        ///
        /// **One submit measures the clock, not the shader.** This machine's GPU idles at 315 MHz
        /// and ramps only under load, so the same frame from a cold start has timed anywhere between
        /// 0.37 and 2.1 ms — a spread wider than most changes worth making, and wide enough that two
        /// runs of *identical* code disagree by more than an A and a B do.
        ///
        /// **And it is high as well as unstable**, which is the half that misleads rather than
        /// merely frustrates: the view whose cold submit read 0.485 ms settles at 0.19 once the
        /// first submit's own costs are behind it. Tracing repeatedly inside one device session is
        /// what makes a difference visible — the best run is the least contended, and the spread
        /// beside it says whether to believe it.
        ///
        /// Eight costs about four milliseconds against a quarter of a second of device setup, so the
        /// default is the honest number rather than the fast one. A real comparison wants hundreds.
        /// A shot traces at least once whatever this says, because a shot is a picture.
        std::uint32_t mRepeat = 8;

        /// Filled in from the cell once it has been read, which is why both commands take their
        /// request by value.
        CellLighting mLighting;

        /// When and in what weather, for the exterior that has a sky. A weather is named as the
        /// fallback settings spell it, and the hour is on a twenty-four hour clock.
        std::string mWeather = "Clear";
        float mHour = 12.0f;

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
