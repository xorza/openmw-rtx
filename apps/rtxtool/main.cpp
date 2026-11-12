#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/openmw/mwrender/rtx/rtxsettings.hpp>
#include <apps/rtxtool/instruments/drivercache.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/benchspec.hpp>
#include <apps/rtxtool/model/film.hpp>
#include <apps/rtxtool/model/maprules.hpp>
#include <apps/rtxtool/model/pictureshelf.hpp>
#include <apps/rtxtool/model/viewfile.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashinstall.hpp>
#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/fixedpath.hpp>
#include <components/misc/result.hpp>
#include <components/platform/platform.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/rtxvulkan/createrenderer.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>

#include "compare.hpp"
#include "framed.hpp"
#include "harnessfolder.hpp"
#include "hosted.hpp"
#include "noise.hpp"
#include "options.hpp"
#include "runfolder.hpp"
#include "toolline.hpp"
#include "verbs.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        constexpr std::string_view applicationName = "RtxTool";

        /// Opens the log, loads the settings and hands the crash catcher where reports go and how long without a
        /// frame is a hang: the game's own sequence (`parseOptions` in `apps/openmw/main.cpp`), so a hang here is
        /// reported as one in the game is.
        void startLogAndSettings(const Files::ConfigurationManager& config)
        {
            Debug::setupLogging(config.getLogPath(), applicationName);
            Debug::setCrashReports(config.getUserDataPath());
            Log(Debug::Info) << Version::getOpenmwVersionDescription();

            Settings::Manager::load(config);
            Crash::setHangLimit(std::chrono::seconds(Settings::general().mCrashHangSeconds));
        }

        /// The layers a run that will be measured or compared gets: none unless it asked, whatever the build default
        /// is. They cost a tenth to half the frame rate, and GPU-assisted validation instruments every shader, so a
        /// number or a picture taken under them is wrong rather than missing.
        Rtx::ValidationOptions validationForMeasuring(const Rtx::ValidationOptions& named)
        {
            return named.mDemanded ? named : Rtx::ValidationOptions{};
        }

        /// The sky the line names for every place it stages: `stopFor` is the rule it feeds.
        StopSky skyGiven(const ToolLine::Sky& sky, const Framed& framed)
        {
            return StopSky{
                .mHour = sky.mHour,
                .mDay = framed.mDay,
                .mWeather = sky.mWeather,
                .mAir = sky.mAir,
            };
        }

        /// Every view a run names, settled: a condition named on the command line is every
        /// place's, and none of them keeps its own.
        std::vector<Stop> stopsFrom(const std::vector<Stop>& views, const ToolLine& line, const Framed& framed)
        {
            const StopSky given = skyGiven(line.mSky, framed);

            std::vector<Stop> stops;
            stops.reserve(views.size());
            for (const Stop& view : views)
                stops.push_back(stopFor(view, given));

            return stops;
        }

        /// Where a verb writes its pictures: `--out`, or a directory named for the verb.
        std::filesystem::path outOf(const Command& command)
        {
            return command.mLine.mOut.value_or(std::filesystem::path(verbName(command.mVerb)));
        }

        /// The whole of a `Framed`, from the command line: the window's settings and the
        /// renderer's setup, read once, so that no verb splits the line again by hand.
        Framed frameFrom(const Command& command)
        {
            const ToolLine::Frame& line = command.mLine.mFrame;

            // A window is the played game with the walls off, so what the player set stands unless an option was
            // typed over it; every other command states its frame, so two runs of it are one run whatever a
            // settings file says. Vertical sync excepted, which no run keeps (`WindowRequest::writeSettings`).
            const bool watched = command.mVerb == Verbs::View;

            Framed framed;
            framed.mWindow.mWidth = line.mSize.mWidth;
            framed.mWindow.mHeight = line.mSize.mHeight;
            framed.mWindow.mFieldOfView = line.mFieldOfView;
            if (watched)
                framed.mWindow.keepPlayersLimit();
            framed.mDay = line.mDay.mValue;
            framed.mMaps = line.mMaps;

            // The settings the ray tracer reads, from one source and through the game's one derivation: the line's
            // where it names them, else the player's for a window or the shipped defaults' for a measured run
            // (`overLine`).
            const MWRender::RtxSettingValues values = overLine(
                line, watched ? MWRender::RtxSettingValues::fromRegistry() : measuredSettings(command.mShipped, line));

            // The world reads the groundcover files only where `[Groundcover] enabled` says
            // (`World::loadGroundcoverFiles`), so the run's answer is written where the world reads
            // it, as the companion maps' rules are.
            Settings::groundcover().mEnabled.set(values.mGroundcover);

            const MWRender::RtxSettings derived = MWRender::RtxSettings::derive(values);
            framed.mSetup.mMirror = derived.mMirror;

            // **The layers the command's row says, unless the line names some**: `VerbPolicy`.
            const Rtx::ValidationOptions& validation = command.mLine.mValidation;
            framed.mSetup.mRun.mValidation
                = policyOf(command.mVerb).mMeasures ? validationForMeasuring(validation) : validation;
            framed.mSetup.mShaders = command.mShaders;
            framed.mSetup.mKernels = Rtx::KernelSet::Every;
            framed.mSetup.mRun.mMemoryBudget = line.mMemoryBudget;
            framed.mSetup.mRun.mPipelineCode = line.mPipelineCode;
            framed.mSetup.mContentStore = std::visit(
                [&](const auto& choice) -> std::optional<std::filesystem::path> {
                    using Choice = std::decay_t<decltype(choice)>;
                    if constexpr (std::is_same_v<Choice, GameContentCache>)
                        return command.mConfig.getCachePath() / Rtx::sContentStoreFolder;
                    else if constexpr (std::is_same_v<Choice, NoContentCache>)
                        return std::nullopt;
                    else
                        return choice;
                },
                line.mContentCache);

            Rtx::RenderProfile& profile = framed.mSetup.mRun.mProfile;
            profile.mRadianceWidth = policyOf(command.mVerb).radianceWidth();
            profile.mShow = line.mShow;
            profile.mExposure = line.mExposure;
            profile.mStressOverlapMs = line.mHold.mValue ? sCheckHoldMs : 0.0;
            profile.mSpecializeKernels = line.mVariants;
            profile.mReconstruction = line.mReconstruction;
            derived.applyTo(profile);

            // What a window's line typed is written where its menu reads it, so a slider opens at what the frame
            // traces and moves it into this run's own settings file (`adoptConfigDirectory`). A window alone: a
            // measured run's renderer holds its own ray count (`commandNoise`), which this would take back.
            if (watched)
            {
                Settings::rtx().mIndirectLighting.set(derived.mIndirectLighting);
                Settings::rtx().mBounceRays.set(static_cast<int>(derived.mBounceRays));
            }

            return framed;
        }

        /// Where someone starts when they have said nothing about where: the ship at Seyda Neen,
        /// where the game starts and the one place every player of it has stood.
        constexpr std::string_view sDefaultView = "seyda-neen-ship";

        /// Whether a run starts from a savegame, which is then what says where the player stands
        /// and what hour and weather it is — unless the line names a view or a cell over it.
        bool startsFromSave(const ToolLine::Places& places)
        {
            return !places.mSave.empty();
        }

        /// The view a run names, or null where it named none and gave a cell instead.
        const Stop* findChosenView(const ToolLine::Places& places, std::vector<Stop>& views)
        {
            std::string name = places.mView;
            if (name.empty())
            {
                if (!places.mCell.empty() || startsFromSave(places))
                    return nullptr;

                name = sDefaultView;
            }

            views = loadViews(viewsFile());
            return &requireView(views, name);
        }

        /// What a `shot` and a `bench --out` write their frames' hashes to, beside the pictures, and
        /// read a reference's from, in the directory `--against` names.
        constexpr std::string_view sRunHashes = "hashes.csv";

        /// Has `request` write its frames' hashes into `out` and read a reference's from `against`,
        /// each where it is named.
        void hashInto(SessionRequest& request, const std::filesystem::path& out, const std::filesystem::path& against)
        {
            if (!out.empty())
                request.mHashes = out / sRunHashes;
            if (!against.empty())
                request.mAgainst = against / sRunHashes;
        }

        /// What `policy` does to one place: the route and the track a command does not follow go, the clock stops
        /// where the row freezes and nothing is flown, and every frame is hashed where the row hashes. A frozen world
        /// makes what one frame differs from the next by the renderer alone, which is what a picture, a digest and a
        /// pixel comparison are about; a view's route is flown with the clock going, because a camera crossing a
        /// stopped world measures streaming. A route that holds the world is the command's own staging and is left.
        void applyPolicy(const VerbPolicy& policy, Stop& stop)
        {
            std::optional<Route>& route = stop.mSchedule.mRoute;
            const bool held = route.has_value() && route->mWorldHeld;
            if (!policy.mFliesRoutes && !held)
                route.reset();
            if (!policy.mFollowsTracks)
                stop.mSchedule.mTrack.reset();

            stop.mSchedule.mFrozen = policy.mFreezes && (!route.has_value() || held);
            stop.mSchedule.mFreeCamera = policy.mPlayed;
            stop.mActions.mHash = stop.mActions.mHash || policy.mHashes;
        }

        /// The one place a `SessionRequest` is built: the stops as the command's row holds them, the
        /// setup the line framed, and what every run reads off the line besides. A verb that wants
        /// more — a suite, a file to write — says so on what comes back.
        SessionRequest sessionFor(const Command& command, const Framed& framed, std::vector<Stop> stops)
        {
            const ToolLine::Session& line = command.mLine.mSession;

            const VerbPolicy& policy = policyOf(command.mVerb);
            for (Stop& stop : stops)
                applyPolicy(policy, stop);

            SessionRequest request;
            request.mStops = std::move(stops);
            request.mSetup = framed.mSetup;
            request.mStep = framed.mStep;
            request.mPlayed = policy.mPlayed;
            request.mMeasures = policy.mMeasures;
            request.mMaps = framed.mMaps;
            request.mHud = line.mHud;
            request.mSpellHands = line.mSpellHands;
            request.mSetup.mInterface = request.mPlayed || request.mHud;
            request.mVanity = line.mVanity;
            request.mNightEye = line.mNightEye;
            request.mRandomSeed = line.mRandomSeed;

            return request;
        }

        /// How long every stop of a run lasts, from what the command line asked for.
        BenchSpec specFrom(const ToolLine::Bench& line)
        {
            return BenchSpec{ .mRun = line.mSpan, .mWarm = BenchSpan{ .mSeconds = line.mWarmup } };
        }

        /// The one place a command names on its line, as a stop: a view, a cell, a save, and
        /// whatever the line says over them.
        Stop stageOnePlace(const Command& command, const Framed& framed)
        {
            const ToolLine::Places& places = command.mLine.mPlaces;
            const ToolLine::Sky& sky = command.mLine.mSky;

            // Holds what the view below points into, for as long as this function needs it.
            std::vector<Stop> views;
            const Stop* found = findChosenView(places, views);
            const CellAddress& cell = places.mCell;

            // A save is the place unless the line names one over it: the stop stands where the save left the player,
            // at its hour, day and weather, where `stopFor` would use a view's noon, clear sky and first day.
            Stop staged;
            if (found == nullptr && cell.empty() && startsFromSave(places))
            {
                staged.mName = Files::pathToUnicodeString(places.mSave.stem());
                staged.mSky.mHour = sky.mHour;
                staged.mSky.mWeather = sky.mWeather;
                staged.mSky.mAir = sky.mAir;
                if (command.mLine.mFrame.mDay.mNamed)
                    staged.mSky.mDay = framed.mDay;
            }
            else
            {
                const Stop view = found != nullptr ? *found : Stop{ .mStand = { .mCell = cell } };
                staged = stopFor(view, skyGiven(sky, framed));
            }

            // Anything given on the command line wins over the view, which is the rule `stopFor`
            // already follows for the hour and the sky.
            if (places.mPos.has_value())
                staged.mStand.mEye = places.mPos;

            if (places.mLook.has_value() && !staged.mStand.lookAt(*places.mLook))
                throw std::runtime_error(
                    "--look faces the camera from an eye, and neither --pos nor the view names one");

            return staged;
        }

        /// The places a picture or a report is made at, each held still: the views `--views` names, or the one place
        /// the line names where it names none. Held still here and not by each command, so a picture the world moved
        /// under cannot come out of a command that forgot the freeze.
        ///
        /// @param frames how many to measure at each place once the world has arrived.
        std::vector<Stop> stagePlaces(const Command& command, const Framed& framed, const std::uint32_t frames)
        {
            const std::optional<std::vector<std::string>>& named = command.mLine.mPlaces.mViews;

            // A place named beside `--views` was refused as the line was read (`ToolOptions::read`).
            std::vector<Stop> stops;
            if (!named.has_value())
                stops.push_back(stageOnePlace(command, framed));
            else
                stops = stopsFrom(chooseViews(loadViews(viewsFile()), *named), command.mLine, framed);

            for (Stop& stop : stops)
                measureFrames(stop, frames);

            return stops;
        }

        /// The places a run of a suite visits, in the order it visits them, with what the suite
        /// says about them.
        struct SuiteRun
        {
            std::vector<Stop> mViews;

            /// Which suite, for the record's own header; empty where `--views` named the places.
            std::string mSuite;

            /// What the suite says about waiting for the ground, or nothing: `BenchSuite::mSettled`.
            std::optional<bool> mSettled;
        };

        /// `--views` beats `--suite`, and both name entries in `views.cfg`. Neither carries coordinates: those live
        /// with the view, so the frame a picture is taken of and the frame a number is measured on stay one frame.
        ///
        /// @param ownSuite the suite the verb runs when neither `--views` nor `--suite` names one:
        ///        `bench` measures the frame budget's places and `check` walks a route as well.
        SuiteRun chooseBenchViews(const ToolLine::Places& places, const std::string_view ownSuite)
        {
            const std::vector<Stop> views = loadViews(viewsFile());

            SuiteRun run;
            std::vector<std::string> wanted;
            if (!places.mViews.has_value())
            {
                run.mSuite = places.mSuite.value_or(std::string(ownSuite));

                const std::vector<BenchSuite> suites = loadSuites(suitesFile());
                const BenchSuite* suite = findSuite(suites, run.mSuite);
                if (suite == nullptr)
                {
                    std::string known;
                    for (const BenchSuite& candidate : suites)
                        known += "\n  " + candidate.mName + "   " + candidate.mNote;

                    throw std::runtime_error("no suite is called \"" + run.mSuite + "\". These are:" + known);
                }

                wanted = suite->mViews;
                run.mSettled = suite->mSettled;
            }
            else
                wanted = *places.mViews;

            run.mViews = chooseViews(views, wanted);
            return run;
        }

        int runListViews()
        {
            for (const Stop& view : loadViews(viewsFile()))
            {
                Debug::getRawStdout() << "  " << view.mName << "\n      " << view.mStand.mCell.getSpelling();

                // A place that fixes a condition is a different frame from the same camera at noon
                // under a clear sky, and this listing is how a view is found.
                if (view.mSky.mHour.has_value())
                    Debug::getRawStdout() << " at " << describeHour(*view.mSky.mHour);

                if (view.mSky.mWeather.has_value())
                    Debug::getRawStdout() << " in " << Rtx::nameOf(*view.mSky.mWeather);

                Debug::getRawStdout() << "\n      " << view.mNote << '\n';
            }

            return 0;
        }

        int commandInfo(const Command& command)
        {
            if (command.mLine.mFolders)
            {
                const Files::FixedPath<> game("openmw");
                Debug::getRawStdout() << "config " << Files::pathToUnicodeString(game.getUserConfigPath()) << '\n'
                                      << "data " << Files::pathToUnicodeString(game.getUserDataPath()) << '\n';
                return 0;
            }

            const Rtx::ValidationOptions& validation = command.mLine.mValidation;

            // A one-pixel target: this reports on a device rather than drawing with it. The shaders are still named,
            // because standing a renderer up compiles one and a build whose shaders are missing should say so here.
            // No pipeline cache, as no verb of this tool keeps one: `RtxRenderer` says why a measured run compiles
            // from source.
            try
            {
                const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createVulkanRenderer(Rtx::RendererOptions{
                    .mShaders = command.mShaders,
                    .mWidth = 1,
                    .mHeight = 1,
                    .mRun = { .mValidation = validation },
                });
                Debug::getRawStdout() << renderer->describeDevice();
                return 0;
            }
            catch (const Rtx::Unsupported& obstacle)
            {
                Debug::getRawStdout() << obstacle.what() << '\n';
                return 1;
            }
        }

        /// What the renderer was handed at each place, without looking at what it drew. Walked twice: a second
        /// whole-graph walk should add nothing, and the report says what it added.
        int commandScene(const Command& command)
        {
            const Framed framed = frameFrom(command);

            std::vector<Stop> stops = stagePlaces(command, framed, 1);
            for (Stop& stop : stops)
            {
                stop.mActions.mFind = command.mLine.mFind;
                stop.mActions.mDigest = stop.mActions.mFind.empty();
                stop.mActions.mWalkTwice = true;
            }

            return runHosted(command, framed, sessionFor(command, framed, std::move(stops)));
        }

        /// The pictures of each place, taken headless: the frame, and the doll, the tile and the sheet where asked
        /// for. The world a player stands in and not one staged here: the cells are read by `MWWorld::Scene`, the
        /// people dressed by `NpcAnimation` and the sky reported by `MWWorld::WeatherManager`. Every view under
        /// `--views`, with `--against` saying which pictures a change moved.
        ///
        /// The frame is judged by its hashes, not its pixels: every frame of a stop is hashed as a `bench --out`
        /// hashes one into `hashes.csv`, and `--against` compares that table first (`FrameHashes` says why a picture
        /// past an upscaler cannot be the verdict). The tile, the doll and the sheet are traced without one and
        /// compared as pictures.
        int commandShot(const Command& command)
        {
            const ToolLine::Shot& line = command.mLine.mShot;
            const Framed framed = frameFrom(command);

            const std::filesystem::path& against = command.mLine.mAgainst;

            // Traced more than once, because one submit measures the clock and not the shader: a GPU idles at a
            // fraction of its clock and ramps only under load. Accumulating replaces repeating rather than joining
            // it, or a run would average eight frames more than it was asked for.
            const std::uint32_t accumulate = line.mAccumulate;
            const std::uint32_t frames = accumulate > 0 ? accumulate : std::max(line.mRepeat, 1u);

            std::vector<Stop> stops = stagePlaces(command, framed, frames);

            const std::filesystem::path out = outOf(command);
            std::filesystem::create_directories(out);

            if (const Result<void, std::string> checked = checkAgainst(out, against); !checked.isOk())
                throw std::runtime_error(checked.error());

            // **What each picture is held to is where it came from.** A frame the wavelet composed and
            // nothing upscaled is the picture the hashes cannot judge; a doll and a tile are always
            // denoised (`Reconstruction::forPicture`); the sheet is the textures and nothing traced.
            const Rtx::RenderProfile& profile = framed.mSetup.mRun.mProfile;
            const PictureRule frameRule = profile.mReconstruction.mDenoise && !Rtx::upscales(profile.mUpscale)
                ? PictureRule::Denoised
                : PictureRule::Hashed;

            const std::string& doll = line.mDoll;
            std::vector<WrittenPicture> written;
            for (Stop& stop : stops)
            {
                stop.mSchedule.mAccumulate = accumulate;

                const auto file = [&](const std::string_view suffix, const PictureRule rule) {
                    written.push_back(
                        WrittenPicture{ .mFile = stop.mName + std::string(suffix) + ".png", .mRule = rule });
                    return out / written.back().mFile;
                };

                stop.mActions.mCapture = Actions::Capture{ .mFile = file("", frameRule) };
                if (!doll.empty())
                    stop.mActions.mDoll = Actions::Doll{ .mWho = doll, .mFile = file("-doll", PictureRule::Denoised) };
                if (line.mMap)
                    stop.mActions.mMapTile = file("-map", PictureRule::Denoised);
                if (line.mTextures)
                    stop.mActions.mSheet = file("-textures", PictureRule::Exact);
            }

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            hashInto(request, out, against);
            PictureShelf drawn;
            if (!against.empty())
                request.mShelf = &drawn;

            // Compared whatever the session answered, because a moved frame is what fails it, and
            // the run where something moved is the run whose tiles, dolls and sheets are wanted.
            const int status = runHosted(command, framed, std::move(request));
            const int compared = compareRuns(drawn, out, against, written);
            return status != 0 ? status : compared;
        }

        int commandBench(const Command& command)
        {
            const ToolLine::Bench& line = command.mLine.mBench;
            Framed framed = frameFrom(command);

            framed.mSetup.mHeadless = !line.mWindow;
            framed.mSetup.mRun.mZoneCounters = line.mCounters;

            const SuiteRun run = chooseBenchViews(command.mLine.mPlaces, "default");
            std::vector<Stop> stops = stopsFrom(run.mViews, command.mLine, framed);

            const BenchSpec spec = specFrom(line);
            const std::vector<Rtx::Weather>& turn = command.mLine.mSky.mTurn;
            // **Nothing written and nothing hashed unless asked**: a bench measures, and a frame read
            // back waits on the device. `--out` is where its hashes go, and its pictures where asked.
            const std::filesystem::path out = command.mLine.mOut.value_or(std::filesystem::path());
            const std::filesystem::path& against = command.mLine.mAgainst;
            const bool pictures = line.mPictures;
            if (pictures && out.empty())
                throw std::runtime_error("--pictures writes into --out, and no --out was named");
            if (!out.empty())
            {
                std::filesystem::create_directories(out);
                if (const Result<void, std::string> checked = checkAgainst(out, against); !checked.isOk())
                    throw std::runtime_error(checked.error());
            }
            const bool hashing = !out.empty() || !against.empty();

            const std::filesystem::path& frameTimes = line.mFrameTimes;
            if (!frameTimes.empty())
                std::filesystem::create_directories(frameTimes);

            for (Stop& stop : stops)
            {
                stop.mSchedule.mSpec = spec;
                stop.mSky.mTurnThrough = turn;
                stop.mActions.mHash = hashing;
                if (!frameTimes.empty())
                    stop.mActions.mFrameTimes = frameTimes / (stop.mName + ".txt");
            }

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mSuite = run.mSuite;
            request.mJson = line.mJson;
            hashInto(request, out, against);
            if (pictures)
                request.mPictures = out;
            request.mPerfControl = line.mPerfControl;
            request.mSetup.mSettled = run.mSettled;

            return runHosted(command, framed, std::move(request));
        }

        /// A window on a place, with the game running behind it.
        ///
        /// The game and not a camera of this tool's own: a window is for seeing how something moves and whether an
        /// artefact is a still or a shimmer, which are questions about the frame a player gets. So the player flies
        /// it, with their own controls and console, in a body with every stat at 255, a Speed of 2000, level 255 and
        /// ten million gold, and the frame rate on the title. Collision comes off, because a view file's coordinates
        /// are where a camera stands rather than where a body fits.
        int commandView(const Command& command)
        {
            Framed framed = frameFrom(command);

            // On the wall, because somebody is watching: a stepped world runs as fast as the card draws it, three
            // times over at two hundred frames a second, and the played game follows the wall.
            framed.mSetup.mHeadless = false;
            framed.mStep = std::nullopt;

            Stop staged = stageOnePlace(command, framed);

            // **A schedule with no end, because somebody is watching.** `--frames` closes it after
            // that many, which is how the window path gets exercised by something that cannot click.
            const std::uint32_t frames = command.mLine.mBench.mFrames;
            staged.mSchedule.mSpec.mRun = BenchSpan{ .mFrames = frames > 0 ? frames : BenchSpan::sUntilClosed };

            std::vector<Stop> stops;
            stops.push_back(std::move(staged));

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mQuitAtEnd = frames > 0;
            request.mKeys = command.mLine.mKeys;
            request.mHomePictures = outOf(command);

            return runHosted(command, framed, std::move(request));
        }

        /// Every claim the tree makes about what the renderer is handed and what it draws, asked of a real game at
        /// each place of a suite: a claim proved against a world of this tool's own would be about a world nobody
        /// plays.
        int commandCheck(const Command& command)
        {
            Framed framed = frameFrom(command);
            if (!command.mLine.mFrame.mHold.mNamed)
                framed.mSetup.mRun.mProfile.mStressOverlapMs = sCheckHoldMs;

            const SuiteRun run = chooseBenchViews(command.mLine.mPlaces, "check");
            std::vector<Stop> stops = stopsFrom(run.mViews, command.mLine, framed);

            const std::span<const Check> every = everyCheck();

            // Every picture a stop can write, at the first place, because each leaves through a path the frame's own
            // passes never touch: the capture's read back, and the tile's and the doll's offscreen traces with
            // targets of their own. Under the layers, this is what finds a barrier they miss.
            const std::filesystem::path out = outOf(command);
            std::filesystem::create_directories(out);
            stops.front().mActions.mCapture = Actions::Capture{ .mFile = out / (stops.front().mName + ".png") };
            stops.front().mActions.mMapTile = out / (stops.front().mName + "-map.png");
            stops.front().mActions.mDoll = Actions::Doll{ "fargoth", out / (stops.front().mName + "-doll.png") };

            for (Stop& stop : stops)
            {
                measureFrames(stop);

                for (const Check check : every)
                    if (canAsk(check, stop, framed.mSetup.mRun.mProfile))
                        stop.mActions.mChecks.push_back(check);

                // A route runs for as long as the line says, and ends where it arrives.
                if (stop.mSchedule.mRoute.has_value())
                    stop.mSchedule.mSpec.mRun = BenchSpan{ .mSeconds = command.mLine.mBench.mSeconds };
            }

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mSuite = run.mSuite;

            return runHosted(command, framed, std::move(request));
        }

        /// How noisy the frame a player sees is, against sixteen frames averaged, and how far what it converges to
        /// stands from the truth, at each place of a suite: five pictures of each place, in one run, and a verdict.
        ///
        /// The reference averages `sNoiseReferenceFrames` frames traced unfiltered and jittered, from the white hash
        /// (a sequence no other picture draws from) and at no texture level epsilon, and is written as the renderer
        /// summed it, at sixteen bits and undithered (`Actions::Capture::Depth::Deep`), so no byte stands under the
        /// bias. The bar averages as many frames as the frame's history holds samples a shown pixel, at most
        /// `sNoiseBarFrames`, unfiltered (`NoiseFrame::mBar`). The frame is the run's own after the warm-up, upscaled
        /// as the run is, or after flying in with `--strafe` or `--walk` (`Stand::approachFrom`); the reference and
        /// the bar are traced with no upscaler at its output size, so an upscaled frame is held to the picture it
        /// stands for. The bar's limit and the frame's mean are `sNoiseMeanDraws` draws each, as pictures shown and
        /// averaged as bytes, so both spreads are measured alike. Every stop at a place draws samples of its own
        /// (`sNoiseSampleStride`), and every picture but the reference holds the exposure the reference ended on.
        ///
        /// With `--versus`, a second side follows at each place: its frame and mean at the first side's draws, and
        /// its own reference, bar and limit only where its unfiltered frames trace otherwise, so an A/B of a switch
        /// only the filters read traces the shared half once.
        ///
        /// Judged against the bar and not a number: a frame is as clean as the bar when it stands no further from its
        /// own mean than the bar from its limit, by the mean and the 99th percentile (`judgeNoise`).
        int commandNoise(const Command& command)
        {
            const ToolLine::Noise& line = command.mLine.mNoise;
            Framed framed = frameFrom(command);

            const SuiteRun run = chooseBenchViews(command.mLine.mPlaces, "noise");
            const std::vector<Stop> places = stopsFrom(run.mViews, command.mLine, framed);

            const std::filesystem::path folder = outOf(command);
            std::filesystem::create_directories(folder);

            const Rtx::ReconstructionRequest played = framed.mSetup.mRun.mProfile.mReconstruction;

            // The other side, where the line names one: the frame and its mean again with one switch
            // of the reconstruction changed, at the same draws (`ToolOptions::versus`).
            const std::string& asked = line.mAsked;
            const std::optional<Rtx::ReconstructionRequest>& versus = line.mVersus;

            // **The renderer's count the most either side traces**, which its targets are made for
            // (`Renderer::setBounceRays`): every stop names its side's own (`planNoise`).
            const std::uint32_t mostRays = std::max(played.mSampling.mBounceRays,
                versus.has_value() ? versus->mSampling.mBounceRays : played.mSampling.mBounceRays);

            NoisePlan plan = planNoise(NoiseAsk{
                .mPlaces = places,
                .mFolder = folder,
                .mPlayed = played,
                .mVersus = versus,
                .mStrafe = line.mStrafe,
                .mWalk = line.mWalk,
                .mCut = line.mCut,
                .mStep = worldStep(framed.mStep),
            });
            framed.mSetup.mRun.mProfile.mReconstruction.mSampling.mBounceRays = mostRays;

            SessionRequest request = sessionFor(command, framed, std::move(plan.mStops));
            request.mSuite = run.mSuite;
            PictureShelf drawn;
            request.mShelf = &drawn;
            Rtx::FrameExtents traced;
            request.mTraced = &traced;

            // An earlier run's record goes before this one draws, so a run that ends before it judges
            // leaves none to be read as its own.
            const std::filesystem::path record = folder / sNoiseRecord;
            std::filesystem::remove(record);

            if (const int status = runHosted(command, framed, std::move(request)); status != 0)
                return status;

            // What the session settled the bars' stops to, from the extents the run was traced at.
            const std::uint32_t barFrames = plan.mBar.under(traced);
            std::array<std::vector<NoiseFigures>, 2> measured;
            int judged = judgeNoise(drawn, folder, plan.mSides, barFrames, measured[0]);
            if (versus.has_value())
            {
                Debug::getRawStdout() << std::format("versus --{}{}{}\n", asked,
                    plan.mOwnBar ? ", against a bar of its own" : "",
                    plan.mOwnReference ? " and a reference of its own" : "");
                judged = std::max(judged, judgeNoise(drawn, folder, plan.mVersusSides, barFrames, measured[1]));
            }

            const std::span<const std::vector<NoiseFigures>> written(measured.data(), versus.has_value() ? 2 : 1);
            if (const Result<void, std::string> wrote = writeNoiseRecord(record, written); !wrote.isOk())
            {
                Debug::getRawStdout() << wrote.error() << '\n';
                return 1;
            }
            return judged;
        }

        /// A film of the keys a window wrote: every take drawn headless, its frames numbered through the film, then
        /// encoded. The plan comes first, whatever follows: a film is an hour of rendering, and a key too far away
        /// shows in the lengths before a frame is drawn. Stepped at the film's own rate, settled and natively
        /// reconstructed (`sFilmUpscale`), with the layers off unless named: the world moves a frame's worth between
        /// frames, no cell arrives on screen, and every pixel is traced.
        int commandFilm(const Command& command)
        {
            const ToolLine::Film& line = command.mLine.mFilm;
            Framed framed = frameFrom(command);

            const std::filesystem::path& keys = command.mLine.mKeys;
            if (keys.empty())
                throw std::runtime_error("a film needs --keys=<file>, the keys `view --keys` appends on Home");

            // **The step is the run's, and the film counts every length in it**: the world moves a
            // frame of film between two frames.
            FilmPacing pacing = line.mPacing;
            framed.mStep = pacing.mStep;
            framed.mSetup.mSettled = true;
            framed.mSetup.mRun.mProfile.mUpscale = sFilmUpscale;

            pacing.mFieldOfView = framed.mWindow.mFieldOfView;
            pacing.mAspect = static_cast<float>(framed.mWindow.mWidth) / static_cast<float>(framed.mWindow.mHeight);
            pacing.mDay = framed.mDay;
            pacing.mTurn = command.mLine.mSky.mTurn;

            const FilmPlan plan = planFilm(loadKeys(keys), pacing);
            Debug::getRawStdout() << describePlan(plan) << std::flush;
            if (line.mPlan)
                return 0;

            const std::filesystem::path directory = outOf(command);
            const std::filesystem::path frames = directory / "frames";
            std::filesystem::create_directories(frames);
            if (const std::size_t cleared = clearFrames(frames); cleared > 0)
                Debug::getRawStdout() << std::format("cleared {} frames of the last film\n", cleared);

            if (const int status = runHosted(command, framed, sessionFor(command, framed, stopsFor(plan, frames)));
                status != 0)
                return status;

            std::filesystem::path video = directory / keys.stem();
            video += ".mp4";
            const std::string encode = encodeCommand(frames, video, plan.getFrames(), pacing.getRate());
            if (!line.mEncode)
            {
                Debug::getRawStdout() << "\nthe frames are in " << Files::pathToUnicodeString(frames)
                                      << "; to encode them:\n"
                                      << encode << '\n';
                return 0;
            }

            Debug::getRawStdout() << "\nencoding: " << encode << '\n' << std::flush;
            if (!Platform::Process::runShell(encode).succeeded())
            {
                Debug::getRawStdout() << "the encoder failed; the frames are in " << Files::pathToUnicodeString(frames)
                                      << '\n';
                return 1;
            }

            Debug::getRawStdout() << "the film is " << Files::pathToUnicodeString(video) << '\n';
            return 0;
        }

        /// One verb: which command it is, the line `--help` prints for it, and what it does. Identified by enum and
        /// not by name, because `verbs.hpp` holds the names and an option says which commands read it in the same
        /// terms, so a command whose options nothing reaches cannot drift in.
        struct Verb
        {
            Verbs mVerb;
            std::string_view mSummary;
            int (*mRun)(const Command&);
        };

        /// Every command there is, in the order `--help` prints them. One list and not two, so a verb cannot be a
        /// command nobody can find or a line of help nothing answers.
        constexpr std::array<Verb, 8> sVerbs{
            Verb{ Verbs::Info, "report the device this renderer would run on", commandInfo },
            Verb{ Verbs::Scene, "read a place and report what the renderer would be handed", commandScene },
            Verb{ Verbs::Shot,
                "the pictures of a place, with no window: the frame, a doll, a map tile, a texture sheet",
                commandShot },
            Verb{ Verbs::View, "open a window on a place and fly around it", commandView },
            Verb{ Verbs::Bench, "time a run of frames at each place of a suite", commandBench },
            Verb{ Verbs::Check, "assert what the renderer is handed and what it draws, at every place of a suite",
                commandCheck },
            Verb{ Verbs::Film, "fly through the keys `view --keys` wrote, into frames and a video", commandFilm },
            Verb{ Verbs::Noise,
                "how noisy the frame is against 16 frames averaged, and how biased, at every place of a suite",
                commandNoise },
        };

        void printUsage(const bpo::options_description& options)
        {
            Debug::getRawStdout() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                                     "Usage: openmw-rtxtool <command> [options]\n\n"
                                     "Commands:\n";

            for (const Verb& verb : sVerbs)
                Debug::getRawStdout() << std::format("  {:<8} {}\n", verbName(verb.mVerb), verb.mSummary);

            Debug::getRawStdout()
                << "\nWith no arguments at all: a window on the ship at Seyda Neen, where the game starts.\n"
                   "The player's configuration is read and never written: what the engine saves on its way\n"
                   "out -- its settings, its log, its key bindings, its Lua storage -- goes to a directory of\n"
                   "this tool's own under the cache path.\n\n"
                << options;
        }

        int dispatch(int argc, char* argv[])
        {
            Platform::init();

            // The verb is taken off the command line rather than declared positional:
            // `ConfigurationManager::readConfiguration` looks every key of the variables map up in the description,
            // so a hidden positional makes it throw. With no verb, a window is what this is for.
            const bool hasVerb = argc >= 2 && argv[1][0] != '-';
            const std::string_view command = hasVerb ? argv[1] : "view";

            const ToolOptions options
                = makeOptions(Rtx::sValidationByDefault ? Rtx::ValidationLevel::Sync : Rtx::ValidationLevel::Off);

            // Boost skips the first token as the program name; with a verb, that token is the verb. The parsed line
            // is held because a variables map cannot tell an option somebody wrote from one `openmw.cfg` set or one
            // that came back defaulted, and a command has to refuse the first.
            const bpo::parsed_options line = hasVerb
                ? bpo::command_line_parser(argc - 1, argv + 1).options(options.mDescription).run()
                : bpo::command_line_parser(argc, argv).options(options.mDescription).run();

            bpo::variables_map variables;
            bpo::store(line, variables);
            bpo::notify(variables);

            if (variables["help"].as<bool>())
            {
                printUsage(options.mDescription);
                return 0;
            }

            Files::ConfigurationManager config;

            // Before the chain is walked, because this is the directory the engine writes into
            // (`ownConfigDirectory`); the log below lands there too.
            const std::filesystem::path own = ownConfigDirectory(config);
            sweepEndedRuns(own.parent_path());
            adoptConfigDirectory(variables, own);

            config.processPaths(variables, std::filesystem::current_path());
            config.readConfiguration(variables, options.mDescription);
            startLogAndSettings(config);

            // **Every verb on one kind of core**, before any thread of the run starts. Eight legs of
            // `one-cell-walk` in turn: the walk's p99 read 1.75 to 1.85 ms in three of the four held
            // to the performance cores, and 2.42 to 2.96 ms in the four the system placed.
            if (const std::size_t kept = Platform::Process::keepToPerformanceCores(); kept > 0)
                Log(Debug::Info) << "Kept to the " << kept << " logical CPUs of the performance cores";

            // Before the verb, as `--help` is: a switch that answers instead of the command is one
            // the command never sees.
            if (variables["list-views"].as<bool>())
                return runListViews();

            const Verbs chosen = verbNamed(command);
            const auto found = std::find_if(
                sVerbs.begin(), sVerbs.end(), [chosen](const Verb& verb) { return verb.mVerb == chosen; });

            if (found == sVerbs.end())
            {
                Debug::getRawStdout() << "Unknown command: " << command << "\n\n";
                printUsage(options.mDescription);
                return 1;
            }

            // Before the command runs: every option is declared on one description, so a command took every one and
            // read those it knew, and `shot --views=balmora` rendered the default view without a word.
            if (const std::string complaint = options.complainAbout(line, chosen); !complaint.empty())
            {
                Debug::getRawStdout() << complaint;
                return 1;
            }

            // **The line read once, into what each option means**, once the configuration filled
            // what it left: every verb reads this, and none looks an option up by its name.
            const ToolLine read = options.read(variables, chosen);

            // **Before any verb makes a device, because the driver reads where its cache is once.**
            // A cache of the shaders this run reads and of nothing else (`DriverCache`).
            const HarnessShaders shaders(found->mVerb, read.mShaderSource);
            const DriverCache driverCache(harnessDirectory(), shaders.getName(), shaders.getSet().mDigest);
            driverCache.applyToDriver();
            driverCache.sweep();

            // Read once for the run, which every framed verb asks three or more of.
            const ShippedDefaults shipped(config);
            return found->mRun(Command{ read, config, shaders.getSet(), found->mVerb, shipped });
        }

        int run(int argc, char* argv[])
        {
            // Failures are reported here rather than left to `Debug::wrapApplication`, which puts up
            // an SDL message box when stdin is not a terminal. This tool is meant to be usable over
            // ssh and from a script, where a dialog nobody can see is a hang.
            try
            {
                return dispatch(argc, argv);
            }
            catch (const std::exception& e)
            {
                Debug::getRawStderr() << "openmw-rtxtool: " << e.what() << '\n';
                return 1;
            }
        }
    }
}

int main(int argc, char* argv[])
{
    // **Huge pages before anything else**, since the process this starts keeps no thread and no
    // state of this one: the figures a measured run reports move run to run on small ones.
    Platform::Process::restartOnHugePages(argv);

    // The catcher's box is off and the catcher is on: a dialog waiting for a click is a run that never finishes,
    // and a measured run that crashes or hangs should still leave the report a player's game would. Not
    // overwritten, so a shell can still ask for the box, or turn the catcher off.
    Platform::Process::setEnvironmentDefault("OPENMW_CRASH_DIALOG", "0");

    return Debug::wrapApplication(RtxTool::run, argc, argv, RtxTool::applicationName);
}
