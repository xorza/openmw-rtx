#include "bench.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <SDL.h>

#include <components/debug/debugging.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/wavespectrum.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

#include "lighting.hpp"
#include "perfcontrol.hpp"
#include "stagedworld.hpp"
#include "window.hpp"
#include "world.hpp"

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        /// What the world advances per frame, which is `PosedActors`' own step and not a second
        /// opinion about it: an actor has to move the same amount per frame here as it does in the
        /// game, and two clocks that disagreed would make a run irreproducible in the one way that
        /// matters.
        constexpr float sStepRate = 60.0f;

        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        double megabytes(std::uint64_t bytes)
        {
            return static_cast<double>(bytes) / (1024.0 * 1024.0);
        }

        /// One line of six figures under the header `heading`.
        std::string describeTimes(std::string_view heading, const FrameTimes& times)
        {
            return std::format("  {:<9}{:9.2f}{:10.2f}{:10.2f}{:10.2f}{:10.2f}{:10.2f}\n", heading, times.mMedian,
                times.mMean, times.mP95, times.mP99, times.mBest, times.mWorst);
        }

        void report(const BenchPlace& place)
        {
            out() << '\n' << place.mView;
            if (!place.mNote.empty())
                out() << " — " << place.mNote;

            out() << '\n'
                  << std::format(
                         "  cell {}   {} instances ({} cutouts)   {:.1f} MiB structures   {} textures, "
                         "{:.1f} MiB\n",
                         place.mCell, place.mScene.mInstances, place.mScene.mCutoutInstances,
                         megabytes(place.mScene.mStructureBytes), place.mScene.mTextureCount,
                         megabytes(place.mScene.mTextureBytes))
                  << std::format("  build {:.0f} ms   {:.1f}% of primary rays hit\n", place.mBuildMs, place.mHitPercent)
                  << std::format("  {:<9}{:>9}{:>10}{:>10}{:>10}{:>10}{:>10}\n", "", "median", "mean", "p95", "p99",
                         "best", "worst")
                  << describeTimes("frame ms", place.mFrame) << describeTimes("trace ms", place.mTrace)
                  << describeTimes("place ms", place.mPlace);

            // **The device's own account of the same frame, medians only.** Six distributions would
            // be a wall; what this row answers is "which of them is the expensive one", and the row
            // above already says how much the whole frame varies.
            if (!place.mGpu.empty())
            {
                out() << "  gpu ms  ";
                for (const GpuZone& zone : place.mGpu)
                    out() << std::format("  {} {:.2f}", zone.mName, zone.mTimes.mMedian);

                out() << '\n';
            }

            out() << std::format("  {} frames in {:.2f} s — {:.1f} fps, {:.1f} at the 1% low\n", place.mFrames,
                place.mWallSeconds, place.mFrame.getRate(), place.mFrame.getLowRate());
        }

        std::string asJson(const FrameTimes& times)
        {
            return std::format(
                R"({{"median": {:.4f}, "mean": {:.4f}, "p95": {:.4f}, "p99": {:.4f}, "best": {:.4f}, "worst": {:.4f}}})",
                times.mMedian, times.mMean, times.mP95, times.mP99, times.mBest, times.mWorst);
        }

        /// Writes the run as one record, for comparing against the same run on another commit.
        ///
        /// Hand-written rather than through a library: this is a flat object of numbers, and the
        /// alternative is a dependency for the sake of six lines.
        void writeJson(const std::filesystem::path& path, const BenchRequest& request, const Rtx::FrameExtents& extents,
            bool validating, const std::vector<BenchPlace>& places)
        {
            std::ofstream file(path);

            file << "{\n"
                 << std::format(R"(  "suite": "{}",)", request.mSuite) << '\n'
                 << std::format(R"(  "output": [{}, {}],)", extents.mOutputWidth, extents.mOutputHeight) << '\n'
                 << std::format(R"(  "render": [{}, {}],)", extents.mRenderWidth, extents.mRenderHeight) << '\n'
                 << std::format(R"(  "upscale": "{}",)", Rtx::upscaleName(request.mUpscale)) << '\n'
                 << std::format(R"(  "frames": {}, "warmup": {}, "validation": {},)", request.getMeasured(),
                        request.getWarmup(), validating)
                 << '\n'
                 << R"(  "places": [)" << '\n';

            for (std::size_t at = 0; at < places.size(); ++at)
            {
                const BenchPlace& place = places[at];
                file << std::format(R"(    {{"view": "{}", "cell": "{}", "instances": {}, "buildMs": {:.2f}, )",
                    place.mView, place.mCell, place.mScene.mInstances, place.mBuildMs)
                     << std::format(R"("frames": {}, "wallSeconds": {:.4f}, "hitPercent": {:.2f}, )", place.mFrames,
                            place.mWallSeconds, place.mHitPercent)
                     << R"("frameMs": )" << asJson(place.mFrame) << R"(, "traceMs": )" << asJson(place.mTrace)
                     << R"(, "placeMs": )" << asJson(place.mPlace) << R"(, "gpuMs": {)";

                for (std::size_t zone = 0; zone < place.mGpu.size(); ++zone)
                    file << std::format(R"({}"{}": {})", zone == 0 ? "" : ", ", place.mGpu[zone].mName,
                        asJson(place.mGpu[zone].mTimes));

                file << "}}" << (at + 1 < places.size() ? "," : "") << '\n';
            }

            file << "  ]\n}\n";
        }

        /// Whether someone has asked for the run to stop. Events are pumped whether or not there is
        /// a window: SDL is initialised either way, and a window nobody drains stops being drawn by
        /// the compositor and starts being reported as hung.
        bool interrupted()
        {
            bool stop = false;
            SDL_Event event;
            while (SDL_PollEvent(&event) != 0)
            {
                if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                    stop = true;
            }

            return stop;
        }
    }

    std::uint32_t BenchRequest::getMeasured() const
    {
        if (mFrames > 0)
            return mFrames;

        return std::max(1u, static_cast<std::uint32_t>(std::lround(mSeconds * sStepRate)));
    }

    std::uint32_t BenchRequest::getWarmup() const
    {
        return static_cast<std::uint32_t>(std::lround(std::max(mWarmup, 0.0f) * sStepRate));
    }

    int runBench(World& world, const Rtx::ValidationOptions& validation, const BenchRequest& request)
    {
        const std::uint32_t measured = request.getMeasured();
        const std::uint32_t warmup = request.getWarmup();

        PerfControl profiling(request.mPerfControl);

        std::unique_ptr<Window> window;
        if (request.mWindow)
            window = std::make_unique<Window>("OpenMW RTX - bench", request.mWidth, request.mHeight);

        std::string reason;
        // **One renderer for the whole run.** Standing one up compiles every pipeline and costs a
        // quarter of a second; doing that per place would put a cold device in front of every
        // measurement and make the first place in the list systematically the slowest.
        const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createRenderer(
            Rtx::RendererOptions{
                .mShaderDirectory = request.mShaderDirectory,
                .mWidth = request.mWidth,
                .mHeight = request.mHeight,
                .mUpscale = request.mUpscale,
                .mWindow = window == nullptr ? nullptr : window->getHandle(),
                .mValidation = validation,
            },
            reason);
        if (renderer == nullptr)
        {
            out() << reason << '\n';
            return 1;
        }

        const Rtx::FrameExtents extents = renderer->getExtents();

        out() << std::format("bench: {} {}, {} frames each ({:.1f} s of world at {:.0f} Hz) after {} warming up\n",
            request.mViews.size(), request.mViews.size() == 1 ? "place" : "places", measured,
            static_cast<double>(measured) / static_cast<double>(sStepRate), sStepRate, warmup);

        out() << std::format("       {}x{}", extents.mOutputWidth, extents.mOutputHeight);
        if (extents.mRenderWidth != extents.mOutputWidth || extents.mRenderHeight != extents.mOutputHeight)
            out() << std::format(" traced at {}x{}", extents.mRenderWidth, extents.mRenderHeight);

        out() << std::format(", upscale {}", Rtx::upscaleName(request.mUpscale));

        // **Said before the run rather than after it.** A figure measured under the layers is not
        // one to compare against anything, and finding that out at the end is finding it out after
        // the ten minutes have been spent.
        if (renderer->isValidating())
            out() << ", WITH THE VALIDATION LAYERS ON — pass --validation=false for a number worth quoting";

        out() << "\n";

        std::vector<BenchPlace> places;
        places.reserve(request.mViews.size());

        std::vector<double> frameTimes;
        std::vector<double> traceTimes;
        std::vector<double> placeTimes;
        frameTimes.reserve(measured);
        traceTimes.reserve(measured);
        placeTimes.reserve(measured);

        bool stopped = false;

        for (const View& view : request.mViews)
        {
            const ESM::Cell* cell = world.findCell(view.mCell);
            if (cell == nullptr)
            {
                out() << "\n" << view.mName << ": no cell called \"" << view.mCell << "\"\n";
                return 1;
            }

            if (window != nullptr)
                window->setTitle("OpenMW RTX - bench - " + view.mName);

            StagedWorld staged(world, *cell,
                StagingRequest{
                    .mRadius = request.mRadius,
                    .mWeather = request.mWeather,
                    .mHour = request.mHour,
                    .mFieldOfView = request.mFieldOfView,
                    .mOrigin = view.mOrigin,
                    .mTarget = view.mTarget,
                },
                request.mActors);

            if (staged.empty())
            {
                out() << "\n" << view.mName << ": the region placed no geometry\n";
                return 1;
            }

            const Clock::time_point buildStart = Clock::now();
            {
                // Held only across the call: the descriptions span the bridge's storage until the
                // upload behind `setScene` has finished with them.
                const RtxBridge::SceneTextures described(staged.getScene(), world.getImageManager());
                renderer->setScene(staged.getScene(), described.getDescriptions(), Rtx::SeaState{});
            }
            const double buildMs = std::chrono::duration<double, std::milli>(Clock::now() - buildStart).count();

            const float far = std::max(staged.getScene().getBounds().radius() * 8.0f, 10000.0f);

            frameTimes.clear();
            traceTimes.clear();
            placeTimes.clear();

            // Per place, because the zones a place has are the zones its content asked for: an
            // interior with nothing moving in it never places and never reports one.
            GpuBreakdown gpu;

            std::uint32_t hits = 0;

            // Restarted when the warmup ends, so `mWallSeconds` covers the frames `mFrames`
            // counts. A clock left running from here would divide six hundred frames by the time
            // six hundred and sixty took, and the sixty are the slow ones.
            Clock::time_point runStart = Clock::now();

            for (std::uint32_t frame = 0; frame < warmup + measured; ++frame)
            {
                // Pumped outside the timing: SDL is what keeps a window being drawn rather than
                // reported as hung, and it is no part of what the renderer costs.
                if (interrupted())
                {
                    stopped = true;
                    break;
                }

                if (frame == warmup)
                {
                    runStart = Clock::now();
                    profiling.enable();
                }

                const Clock::time_point frameStart = Clock::now();

                // **After the first, which is the frame `setScene` already built**, and by frame
                // index rather than by the clock: a world stepped by how long the last frame took
                // would render a different sequence on every machine and on every build.
                double placeMs = 0.0;
                if (frame > 0 && staged.getMotion() != nullptr && staged.getMotion()->step(frame))
                {
                    const Clock::time_point placeStart = Clock::now();
                    renderer->placeScene(staged.getScene(), Rtx::SeaState{});
                    placeMs = std::chrono::duration<double, std::milli>(Clock::now() - placeStart).count();
                }

                const Rtx::FrameExtents shown = renderer->getExtents();
                Rtx::Shaders::VisibilityConstants constants = Rtx::makeCamera(staged.getPlacement().mOrigin,
                    staged.getPlacement().mTarget, request.mFieldOfView, shown.mRenderWidth, shown.mRenderHeight, far);
                constants.mDelight = request.mDelight;

                CellLighting lighting = staged.getLighting();
                lighting.mSeconds = static_cast<float>(frame) / sStepRate;
                applyLighting(lighting, constants);

                // What the upscaler's sample sequence and every random draw in the shader are walked
                // by. Held to the frame index so the same run draws the same samples twice over.
                constants.mFrame = frame;

                const Rtx::FrameResult result = renderer->renderFrame(
                    constants, Rtx::FrameOptions{ .mFilter = request.mFilter, .mExposure = request.mExposure });

                if (window != nullptr && !renderer->presentFrame())
                    renderer->resize(window->getWidth(), window->getHeight());

                const double frameMs = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();

                if (frame >= warmup)
                {
                    frameTimes.push_back(frameMs);
                    traceTimes.push_back(result.mTraceMs);
                    placeTimes.push_back(placeMs);
                    gpu.add(result.mGpu);
                    hits = result.mHits;
                }
            }

            const Clock::time_point runEnd = Clock::now();
            profiling.disable();

            if (frameTimes.empty())
                break;

            const Rtx::FrameExtents traced = renderer->getExtents();
            const double pixels = static_cast<double>(traced.mRenderWidth) * traced.mRenderHeight;

            // Once: summarising sorts the rows in place, so a second call would be re-sorting what
            // the first one's iterators point at.
            const std::span<const GpuZone> zones = gpu.summariseZones();

            places.push_back(BenchPlace{
                .mView = view.mName,
                .mCell = view.mCell,
                .mNote = view.mNote,
                .mBuildMs = buildMs,
                .mFrames = static_cast<std::uint32_t>(frameTimes.size()),
                .mWallSeconds = std::chrono::duration<double>(runEnd - runStart).count(),
                .mFrame = summarise(frameTimes),
                .mTrace = summarise(traceTimes),
                .mPlace = summarise(placeTimes),
                .mGpu = std::vector<GpuZone>(zones.begin(), zones.end()),
                .mHitPercent = static_cast<double>(hits) / pixels * 100.0,
                .mScene = renderer->getSceneStats(),
            });

            report(places.back());
        }

        if (places.empty())
        {
            out() << "\nstopped before anything was measured\n";
            return 1;
        }

        std::uint32_t frames = 0;
        double lasted = 0.0;
        for (const BenchPlace& place : places)
        {
            frames += place.mFrames;
            lasted += place.mWallSeconds;
        }

        out() << std::format("\n{} {}, {} frames in {:.1f} s{}\n", places.size(),
            places.size() == 1 ? "place" : "places", frames, lasted, stopped ? " — stopped early" : "");

        if (!request.mJson.empty())
        {
            writeJson(request.mJson, request, extents, renderer->isValidating(), places);
            out() << "wrote " << Files::pathToUnicodeString(request.mJson) << '\n';
        }

        return 0;
    }
}
