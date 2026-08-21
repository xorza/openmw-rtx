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

#include <components/rtx/renderer.hpp>

namespace Rtx
{
    class SceneDesc;
}

namespace RtxTool
{
    /// Everything the screenshot needs that is not the world itself.
    struct ShotRequest
    {
        std::filesystem::path mOutput;
        std::filesystem::path mShaderDirectory;

        /// The size the picture is written at. What it is traced at follows from `mUpscale`, and
        /// the summary line says so whenever the two differ.
        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;
        float mFieldOfView = 60.0f;

        /// Whether Ray Reconstruction stands between the trace and the picture, and how hard it
        /// works. It denoises for itself, so `mFilter` stops meaning anything once this is on.
        Rtx::Upscale mUpscale = Rtx::Upscale::Off;

        /// How much of the lighting painted into each texture to divide back out, from zero to one.
        /// Zero shows the textures as they were drawn, with their lighting still in them.
        float mDelight = 1.0f;

        /// Whether each frame samples a different point inside its pixel.
        ///
        /// Only worth anything to something putting several frames together: with `--accumulate` it
        /// turns a converged reference into an antialiased one, and it is what an upscaler
        /// reconstructs detail from.
        bool mJitter = false;

        /// Whether the denoiser runs. Off is how a reference is made, and how the noise the filter
        /// is meant to remove can be looked at.
        bool mFilter = true;

        /// Write the albedo with no shading over it.
        bool mShowAlbedo = false;

        /// What to scale the frame by before the display curve, or nothing to measure it off the
        /// frame. A picture wants it measured; a reference wants it held still.
        std::optional<float> mExposure;

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

        /// How many differently-seeded frames to average into the picture. Zero leaves `mRepeat` in
        /// charge, and the picture is one frame however many times that traced it.
        ///
        /// **A converged reference, which is the only ground truth a sampled renderer has.** One
        /// bounce per pixel estimates an integral without bias, so enough of them average to the
        /// value itself — and there is nothing else to compare a denoised frame against, since the
        /// answer cannot be written down. Error falls as the square root of this, so four times the
        /// frames halves it: a hundred is a clean picture and a thousand is a reference.
        ///
        /// **Not the same knob as `mRepeat`**, which traces one frame over and over to time it. This
        /// advances the seed, so every trace is a different sample and the picture improves; timing
        /// a run of these measures the accumulation as well as the trace.
        std::uint32_t mAccumulate = 0;

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
        const Rtx::ValidationOptions& validation, const ShotRequest& request);
}
