#include "shot.hpp"

#include "lighting.hpp"
#include "placement.hpp"
#include "png.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double millisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        /// What a run of traces of the same frame came to.
        struct TraceTimes
        {
            double mBest;
            double mMedian;
            double mWorst;
        };

        /// The three figures a run of traces is worth quoting by.
        ///
        /// **The best is the answer and the spread is whether to believe it.** A minimum over enough
        /// runs is the least-contended one and the most repeatable; a median an order away from it
        /// says the machine was doing something else, and the number should not be quoted.
        ///
        /// **By reference, and it sorts in place.** A copy would read better at the call site and
        /// cannot be had: taking one loses the compiler its proof that the loop below pushed at
        /// least once, and `front()` on a vector it can no longer see into is a hard warning. The
        /// alternative is a defensive branch for a case the caller cannot produce.
        TraceTimes summarise(std::vector<double>& times)
        {
            std::sort(times.begin(), times.end());
            return TraceTimes{
                .mBest = times.front(),
                .mMedian = times[times.size() / 2],
                .mWorst = times.back(),
            };
        }
    }

    int renderShot(const Rtx::SceneDesc& scene, Resource::ImageManager& images,
        const Rtx::ValidationOptions& validation, const ShotRequest& request)
    {
        std::ostream& out = Debug::getRawStdout();

        if (scene.getInstances().empty())
        {
            out << "Nothing to render: the cell placed no geometry.\n";
            return 1;
        }

        // Walks every mesh, so it is asked for once and the answer kept.
        const osg::BoundingBoxf bounds = scene.getBounds();
        const Placement placement = placeCamera(bounds, request.mFieldOfView, request.mOrigin, request.mTarget);

        const Clock::time_point deviceStart = Clock::now();
        std::string reason;
        const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createRenderer(
            Rtx::RendererOptions{
                .mShaderDirectory = request.mShaderDirectory,
                .mWidth = request.mWidth,
                .mHeight = request.mHeight,
                .mUpscale = request.mUpscale,
                .mValidation = validation,
            },
            reason);
        if (renderer == nullptr)
        {
            out << reason << '\n';
            return 1;
        }
        const double deviceMs = millisecondsSince(deviceStart);

        // **Asked rather than assumed.** `--size` is what the picture comes out at; what the trace
        // runs at is the upscaler's answer for that, and the camera's per-pixel ray spread is
        // derived from it.
        const Rtx::FrameExtents extents = renderer->getExtents();

        const Clock::time_point buildStart = Clock::now();
        // The bridge decodes and describes; the backend uploads. Held because the descriptions span
        // its storage until the upload has finished.
        const RtxBridge::SceneTextures described(scene, images);
        renderer->setScene(scene, described.getDescriptions(), Rtx::SeaState{});
        const double buildMs = millisecondsSince(buildStart);
        const Rtx::SceneStats& stats = renderer->getSceneStats();

        // Far enough to cross any cell: the largest exterior view in the game is a few tens of
        // thousands of units, and a primary ray that reaches this has left the world.
        const float far = bounds.radius() * 8.0f;
        Rtx::Shaders::VisibilityConstants camera = Rtx::makeCamera(placement.mOrigin, placement.mTarget,
            request.mFieldOfView, extents.mRenderWidth, extents.mRenderHeight, far);
        camera.mShowAlbedo = request.mShowAlbedo ? 1u : 0u;
        camera.mDelight = request.mDelight;
        applyLighting(request.mLighting, camera);

        // **Accumulating replaces repeating rather than joining it.** A run of `--accumulate=4` that
        // also honoured the repeat default would quietly average eight frames and report four — and
        // a convergence ladder built on that reads as though the first four samples bought nothing,
        // which is exactly what it looked like. A shot traces at least once either way, because a
        // shot is a picture.
        const bool averaging = request.mAccumulate > 0;
        const std::uint32_t frames = averaging ? request.mAccumulate : std::max(request.mRepeat, 1u);

        std::vector<double> traces;
        traces.reserve(frames);

        std::uint32_t hits = 0;

        // **A `do` and not a `for`, for the reason `summarise` takes its argument by reference.** The
        // bound below is `max(..., 1)` and a `for` over it still leaves the compiler unable to prove
        // the body ran, so `front()` on what it filled becomes a hard warning. Looping this way says
        // it structurally.
        std::uint32_t frame = 0;
        do
        {
            // **The seed moves only when the frames are being averaged.** A timing run wants the
            // same work every trace, so that what the spread shows is the machine and not two
            // frames that happened to sample different geometry.
            camera.mFrame = averaging ? frame : 0;

            const Rtx::FrameResult result = renderer->renderFrame(camera,
                Rtx::FrameOptions{
                    .mAccumulate = averaging ? frame + 1 : 0, .mJitter = request.mJitter, .mFilter = request.mFilter });
            traces.push_back(result.mTraceMs);
            hits = result.mHits;
            ++frame;
        } while (frame < frames);

        const TraceTimes trace = summarise(traces);

        std::vector<std::uint8_t> pixels;
        renderer->readPixels(pixels);
        writePng(request.mOutput, extents.mOutputWidth, extents.mOutputHeight, pixels);

        // Primary rays, so out of the pixels that were traced rather than the pixels written.
        const double fraction
            = static_cast<double>(hits) / (static_cast<double>(extents.mRenderWidth) * extents.mRenderHeight) * 100.0;

        out << "wrote " << Files::pathToUnicodeString(request.mOutput) << ' ' << extents.mOutputWidth << 'x'
            << extents.mOutputHeight;

        // Said whenever the two differ, because a trace time is a statement about the size it was
        // traced at and the file gives no hint of it.
        if (extents.mRenderWidth != extents.mOutputWidth || extents.mRenderHeight != extents.mOutputHeight)
            out << ", traced at " << extents.mRenderWidth << 'x' << extents.mRenderHeight;

        out << '\n'
            << "camera:     " << placement.mOrigin.x() << ", " << placement.mOrigin.y() << ", " << placement.mOrigin.z()
            << "  looking at " << placement.mTarget.x() << ", " << placement.mTarget.y() << ", "
            << placement.mTarget.z() << '\n'
            << "primary rays that hit: " << fraction << "%\n"
            << "instances:  " << stats.mInstances << ", of which " << stats.mCutoutInstances << " are cutouts\n"
            << "structures: " << stats.mStructureBytes / 1024 << " KiB\n"
            << "tables:     " << stats.mTableBytes / 1024 << " KiB\n"
            << "textures:   " << stats.mTextureCount << " in " << stats.mTextureBytes / 1024 << " KiB\n"
            << "device up:  " << deviceMs << " ms\n"
            << "build:      " << buildMs << " ms\n"
            << "trace:      " << trace.mBest << " ms";

        if (averaging)
            out << " (best of " << frames << " accumulated; median " << trace.mMedian << ", worst " << trace.mWorst
                << ")";
        else if (frames > 1)
            out << " (best of " << frames << "; median " << trace.mMedian << ", worst " << trace.mWorst << ")";
        else
            out << " (one submit, including the wait)";

        // **Said out loud, because it more than doubles the number above it.** The layers are on by
        // default outside a Release build, and a figure measured under them is not one to compare
        // against anything. Measured over Balmora at 1080p, best of twenty: 3.5 ms with no layers,
        // 3.9 ms with core and synchronization validation, 8.1 ms with GPU-assisted on top — a tenth
        // for the first two and well over double for the third.
        //
        // Anyone quoting a trace time wants `--validation=false`, which turns the two finer switches
        // off along with the layers wherever they were only on by default.
        if (renderer->isValidating())
            out << ", with the validation layers on";

        out << '\n';

        return 0;
    }
}
