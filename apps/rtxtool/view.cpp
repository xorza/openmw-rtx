#include "view.hpp"

#include "viewpoint.hpp"

#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <ostream>
#include <vector>

#include <SDL.h>

#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

#include <set>

#include "cellscene.hpp"
#include "lighting.hpp"
#include "placement.hpp"
#include "window.hpp"
#include <components/esm3/loadcell.hpp>
#include <components/rtxbridge/png.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        /// Two, so the CPU can prepare one frame while the GPU works on the other. More would add
        /// latency to a tool whose whole job is answering a mouse.
        constexpr std::uint32_t sFramesInFlight = 2;

        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        /// Runs its lambda however the scope is left.
        ///
        /// **The frame loop can throw, and what freed its semaphores and fences came after it.** A
        /// `checkVk` failure anywhere in a frame left all seven behind — and with the layers on by
        /// default that is worse than a leak: `vkDestroyDevice` reports them, the abort policy fires
        /// while the stack is still unwinding, and the error that actually happened never reaches
        /// anyone. What the loop broke on is what should be printed.
        template <typename Run>
        class OnScopeExit
        {
        public:
            explicit OnScopeExit(Run run)
                : mRun(std::move(run))
            {
            }

            OnScopeExit(const OnScopeExit&) = delete;
            OnScopeExit& operator=(const OnScopeExit&) = delete;

            ~OnScopeExit() { mRun(); }

        private:
            Run mRun;
        };

        /// Where the camera is standing now, under the conditions the window was opened with.
        Viewpoint spotOf(const ViewRequest& request, const FlyCamera& camera)
        {
            return Viewpoint{
                .mView = request.mView,
                .mNote = request.mNote,
                .mCell = request.mCell,
                .mOrigin = camera.getOrigin(),
                .mTarget = camera.getTarget(),
                .mWeather = request.mWeather,
                .mHour = request.mHour,
            };
        }

        void printHelp()
        {
            out() << "\n"
                     "  W A S D        move,  Q E or ctrl/space for down and up\n"
                     "  right drag     look\n"
                     "  shift / alt    six times faster / seven times slower\n"
                     "  wheel          change the base speed\n"
                     "  P              print this spot as a views.cfg block\n"
                     "  F3             print this spot as a command line, for profiling\n"
                     "  F2             write a screenshot\n"
                     "  F1             this list\n"
                     "  Esc            quit\n\n";
        }
    }

    int runWindow(World& world, const ESM::Cell& centre, const Rtx::ValidationOptions& validation, ViewRequest request,
        const ActorRequest& actors)
    {
        Window window(request.mTitle, request.mWidth, request.mHeight);

        std::string reason;
        const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createRenderer(
            Rtx::RendererOptions{
                .mShaderDirectory = request.mShaderDirectory,
                .mWidth = request.mWidth,
                .mHeight = request.mHeight,
                .mUpscale = request.mUpscale,
                .mWindow = window.getHandle(),
                .mValidation = validation,
            },
            reason);
        if (renderer == nullptr)
        {
            out() << reason << '\n';
            return 1;
        }

        // **Kept across loads, which is the whole of what makes walking into a cell cheap.** The
        // extractor's identity maps resolve geometry met again to the mesh already in the scene, so
        // a ring of new cells adds its own models and reuses everything the region already had.
        // The graph the mirror walks — one group per cell, the actors hung in it, terrain alongside.
        osg::ref_ptr<osg::Group> root = new osg::Group;

        Rtx::SceneDesc scene;
        RtxBridge::SceneExtractor extractor(scene);
        std::set<std::string> loaded;

        /// Brings the region around `around` in, and hands the renderer whatever is now in the
        /// scene. False where the cell placed nothing at all.
        ///
        /// **Nothing is ever taken out.** Removing a cell means removing its instances, and neither
        /// `SceneDesc` nor the acceleration structures can do that yet — so a long walk grows until
        /// the process is restarted. That is a harness being honest about its limits rather than a
        /// design; the game will need the other half.
        std::vector<CellPerson> arrivals;
        std::vector<CellProp> arrivedProps;

        /// Hands the renderer everything now in the scene, structures and textures and all.
        const auto rebuild = [&] {
            const RtxBridge::SceneTextures described(scene, world.getImageManager());
            renderer->setScene(scene, described.getDescriptions(), Rtx::SeaState{});
        };

        /// Reads the region around `around` into the scene, and says whether anything was there.
        ///
        /// **It does not build.** The people who arrive with a ring of cells are bodies nobody has
        /// assembled yet, and their meshes have to be in the scene before the structures are made
        /// from it — a top level naming a mesh with no bottom level behind it is a fatal frame.
        const auto bring = [&](const ESM::Cell& around) {
            const Clock::time_point began = Clock::now();
            const CellReport arrived = readRegion(world, around, *root, loaded, actors.mProps);

            if (arrived.mCells == 0)
                return false;

            // **Built, then walked.** Reading a region puts nodes under the root; nothing is
            // mirrored until something walks them, which is the split the game has too.
            extractor.extract(*root, osg::Matrixf::identity(), 0);

            for (const Rtx::Light& light : arrived.mLights)
                scene.addLight(light);

            arrivals = arrived.mPeople;
            arrivedProps = arrived.mProps;

            out() << std::format("loaded {} cells in {:.2f} s, {} instances now placed\n", arrived.mCells,
                std::chrono::duration<double>(Clock::now() - began).count(), scene.getPlacedCount());

            return true;
        };

        RegionLoad arrivedWith
            = loadRegion(world, centre, *root, scene, loaded, request.mWeather, request.mHour, actors.mProps);

        extractor.extract(*root, osg::Matrixf::identity(), 0);
        request.mLighting = arrivedWith.mLighting;

        if (scene.getPlacedCount() == 0)
        {
            out() << "Nothing to show: the region placed no geometry.\n";
            return 1;
        }

        // Which square the camera stood in when the region around it was last brought in. Crossing
        // out of it is what asks for the next ring.
        std::string standing;

        const osg::BoundingBoxf bounds = scene.getBounds();
        const Placement start = placeCamera(bounds, request.mFieldOfView, request.mOrigin, request.mTarget);

        // **The region's own residents, plus whoever was asked for on the command line.** The row
        // stands in front of where the camera starts and stays there while it flies around them,
        // which is what a window is for; the residents stand where the cell put them.
        const std::span<const CellPerson> residents
            = actors.mResidents ? std::span<const CellPerson>(arrivedWith.mPeople) : std::span<const CellPerson>();

        const std::span<const CellProp> props
            = actors.mProps ? std::span<const CellProp>(arrivedWith.mProps) : std::span<const CellProp>();

        std::unique_ptr<PosedActors> posed;
        if (!actors.empty() || !residents.empty() || !props.empty())
        {
            posed = std::make_unique<PosedActors>(world, scene, extractor, *root, actors);
            posed->addResidents(residents);
            posed->addProps(props);
            posed->addRow(actors, start);

            const RtxBridge::ExtractionStats& settled = posed->settle();
            out() << std::format(
                "{} actors and {} live props placed, {} deforming drawables, {} emitters holding "
                "{} particles\n",
                posed->getCount() - posed->getPropCount(), posed->getPropCount(), settled.mDeformed, settled.mEmitters,
                settled.mSprites);
        }

        // **Once, and after everyone is in.** The bodies brought meshes of their own, so a build that
        // ran before them would leave the frame naming geometry it had no structure for.
        rebuild();

        FlyCamera camera;
        camera.look(start.mOrigin, start.mTarget);

        const float far = std::max(bounds.radius() * 8.0f, 10000.0f);

        printHelp();

        bool running = true;
        bool looking = false;
        bool resized = false;
        std::uint32_t drawn = 0;

        const auto handle = [&](const SDL_Event& event) {
            switch (event.type)
            {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                        resized = true;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_RIGHT)
                    {
                        looking = true;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_RIGHT)
                    {
                        looking = false;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (looking)
                        camera.turn(-event.motion.xrel * 0.0025f, -event.motion.yrel * 0.0025f);
                    break;
                case SDL_MOUSEWHEEL:
                    camera.scaleSpeed(event.wheel.y > 0 ? 1.3f : 1.0f / 1.3f);
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        running = false;
                    else if (event.key.keysym.sym == SDLK_F1)
                        printHelp();
                    else if (event.key.keysym.sym == SDLK_p)
                    {
                        // The readable line above both formats, so a log of them says where each
                        // one is without anything having to parse it back first.
                        const Viewpoint spot = spotOf(request, camera);
                        out() << describeSpot(spot) << describeBlock(spot);
                    }
                    else if (event.key.keysym.sym == SDLK_F3)
                    {
                        const Rtx::FrameExtents shown = renderer->getExtents();
                        out() << describeSpot(spotOf(request, camera))
                              << describeProfile(request, validation, camera.getOrigin(), camera.getTarget(),
                                     shown.mOutputWidth, shown.mOutputHeight)
                              << '\n';
                    }
                    else if (event.key.keysym.sym == SDLK_F2 && drawn > 0)
                    {
                        const Rtx::FrameExtents shown = renderer->getExtents();
                        const std::filesystem::path file
                            = request.mScreenshotDirectory / ("rtx-" + std::to_string(SDL_GetTicks()) + ".png");
                        std::vector<std::uint8_t> pixels;
                        renderer->readPixels(pixels);
                        RtxBridge::writePng(file, shown.mOutputWidth, shown.mOutputHeight, pixels);
                        out() << "wrote " << Files::pathToUnicodeString(file) << '\n';
                    }
                    break;
                default:
                    break;
            }
        };
        Clock::time_point previous = Clock::now();
        const Clock::time_point began = previous;

        // Five times a second: fast enough that the coordinates keep up with the mouse, slow enough
        // that the compositor is not asked to redraw a title bar every frame.
        constexpr auto titleInterval = std::chrono::milliseconds(200);
        Clock::time_point lastTitle = previous;
        std::uint32_t framesSinceTitle = 0;

        while (running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event) != 0)
                handle(event);

            const Clock::time_point now = Clock::now();
            const float seconds = std::chrono::duration<float>(now - previous).count();
            previous = now;
            camera.advance(std::min(seconds, 0.1f));

            ++framesSinceTitle;
            if (now - lastTitle >= titleInterval)
            {
                const double elapsed = std::chrono::duration<double>(now - lastTitle).count();
                const osg::Vec3f at = camera.getOrigin();
                const Rtx::FrameExtents shown = renderer->getExtents();

                std::string sizes = std::format("{}x{}", shown.mOutputWidth, shown.mOutputHeight);
                if (shown.mRenderWidth != shown.mOutputWidth)
                    sizes = std::format("{}x{} to {}", shown.mRenderWidth, shown.mRenderHeight, sizes);

                window.setTitle(std::format("{}  |  {:.0f} fps  |  {}  |  {:.0f}, {:.0f}, {:.0f}  |  {:.0f} u/s",
                    request.mTitle, framesSinceTitle / elapsed, sizes, at.x(), at.y(), at.z(), camera.getSpeed()));

                framesSinceTitle = 0;
                lastTitle = now;
            }

            if (resized)
            {
                renderer->resize(window.getWidth(), window.getHeight());
                resized = false;
            }

            // **The region follows the camera.** Crossing out of the square the last load was
            // centred on brings the ring that is now in range; the ones behind stay, for the reason
            // `bring` gives.
            if (centre.isExterior())
            {
                if (std::string square = cellAt(camera.getOrigin()); square != standing)
                {
                    standing = std::move(square);
                    if (const ESM::Cell* cell = world.findCell(standing))
                    {
                        // The new cells are walked into whatever the scene holds, so the actors come
                        // out first and the snapshot is retaken with the wider world in it. Leaving
                        // them in would place a second copy of everyone on the very next frame.
                        if (posed != nullptr)
                            posed->unplace();

                        arrivals.clear();
                        arrivedProps.clear();
                        if (bring(*cell))
                        {
                            // The snapshot first, then the people who arrived with the ring: the
                            // snapshot is the still world, and a resident belongs to the half of the
                            // scene that is walked in again every frame.
                            if (posed != nullptr)
                            {
                                posed->restanding();
                                if (actors.mResidents)
                                    posed->addResidents(arrivals);
                                if (actors.mProps)
                                    posed->addProps(arrivedProps);

                                posed->settle();
                            }

                            rebuild();
                        }
                    }
                }
            }

            // **The clock the world runs on, not the frame count.** A window that dropped frames
            // would otherwise animate in slow motion, and one that ran fast would gabble.
            if (posed != nullptr
                && posed->advanceTo(static_cast<float>(std::chrono::duration<double>(now - began).count())))
                renderer->placeScene(scene, Rtx::SeaState{});

            const Rtx::FrameExtents extents = renderer->getExtents();
            // The direction rather than `getTarget`, which exists so a person can read `look` in
            // `views.cfg` and tell where it points. Recovering it back out of two world points is
            // rounding, and a flying camera is where that shows.
            Rtx::Shaders::VisibilityConstants constants = Rtx::makeCameraAlong(camera.getOrigin(), camera.getForward(),
                request.mFieldOfView, extents.mRenderWidth, extents.mRenderHeight, far);
            constants.mShowAlbedo = request.mShowAlbedo ? 1u : 0u;
            constants.mDelight = request.mDelight;

            // The window is the only path with a clock. A screenshot leaves the sea still, so two
            // runs of one build agree pixel for pixel.
            CellLighting lighting = request.mLighting;
            lighting.mSeconds = static_cast<float>(std::chrono::duration<double>(now - began).count());
            applyLighting(lighting, constants);

            // What the fog's step jitter varies by, and what the upscaler's sample sequence is
            // walked by. A screenshot leaves it at zero and gets the same frame twice; here it has
            // to move, or twenty-four shells stand still in front of the camera and the jitter hides
            // nothing.
            constants.mFrame = drawn;

            renderer->renderFrame(
                constants, Rtx::FrameOptions{ .mFilter = request.mFilter, .mExposure = request.mExposure });

            if (!renderer->presentFrame())
                resized = true;

            // Counted unconditionally: the summary at the end reports it whether or not a limit
            // was asked for, and `&&` would have skipped the increment in the interactive case.
            ++drawn;
            if (request.mFrames != 0 && drawn >= request.mFrames)
                running = false;
        }

        // One line at the end rather than one a second throughout: a number per second is noise to
        // someone watching the title bar, and scrollback to someone who ran this with --frames.
        const double lasted = std::chrono::duration<double>(Clock::now() - began).count();
        out() << std::format(
            "\n{} frames in {:.2f} s, {:.0f} fps average", drawn, lasted, drawn / std::max(lasted, 1e-6));

        // The same caveat `shot` prints beside its own figure: the layers are on by default outside
        // a Release build and cost about half the frame rate between them, so this is not a number
        // to compare against anything without `--validation=false`.
        if (renderer->isValidating())
            out() << ", with the validation layers on";

        out() << '\n';
        // Where it was left, so a session that ended somewhere worth keeping did not lose it.
        const Viewpoint spot = spotOf(request, camera);
        out() << describeSpot(spot) << describeBlock(spot);

        return 0;
    }
}
