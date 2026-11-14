#include <algorithm>
#include <array>
#include <cassert>
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

#include <boost/program_options/errors.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/openmw/mwrender/rtx/rtxsettings.hpp>
#include <apps/rtxtool/instruments/drivercache.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/exitstatus.hpp>
#include <apps/rtxtool/model/film.hpp>
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
#include "plans.hpp"
#include "runfolder.hpp"
#include "toolline.hpp"
#include "verbs.hpp"
#include "worldmap.hpp"

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

        /// The whole of a `Framed`, from the command line: the window's settings and the renderer's setup, read once,
        /// so that no verb splits the line again by hand. Here and not beside the plans, because it derives the
        /// ray tracer's settings through the game's own rule (`MWRender::RtxSettings`), which a test binary that
        /// links no game cannot reach; a plan is handed what this framed.
        Framed frameFrom(const Command& command)
        {
            const ToolLine::Frame& line = command.mLine.mFrame;

            // A window is the played game with the walls off, so what the player set stands unless an option was
            // typed over it; every other command states its frame, so two runs of it are one run whatever a
            // settings file says. Vertical sync excepted, which no run keeps (`WindowRequest::writeSettings`).
            const bool watched = command.mVerb == Verb::View;

            Framed framed;
            framed.mWindow.mWidth = line.mSize.mWidth;
            framed.mWindow.mHeight = line.mSize.mHeight;
            framed.mWindow.mFieldOfView = line.mFieldOfView;
            if (watched)
                framed.mWindow.keepPlayersLimit();
            framed.mDay = line.mDay.mValue;

            // The settings the ray tracer reads, from one source and through the game's one derivation: the line's
            // where it names them, else the player's for a window or the shipped defaults' for a measured run
            // (`overLine`).
            const MWRender::RtxSettingValues values = overLine(line, [&] {
                if (watched)
                    return MWRender::RtxSettingValues::fromRegistry();
                const MWRender::RtxSettingValues measured = measuredSettings(command.mShipped, line);
                return command.mVerb == Verb::Tour ? touredSettings(command.mShipped, measured) : measured;
            }());

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

        ExitStatus runListViews()
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

            return ExitStatus::Passed;
        }

        ExitStatus commandInfo(const Command& command)
        {
            if (command.mLine.mFolders)
            {
                const Files::FixedPath<> game("openmw");
                Debug::getRawStdout() << "config " << Files::pathToUnicodeString(game.getUserConfigPath()) << '\n'
                                      << "data " << Files::pathToUnicodeString(game.getUserDataPath()) << '\n';
                return ExitStatus::Passed;
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
                Debug::getRawStdout() << renderer->describeDevice() << '\n';
                return ExitStatus::Passed;
            }
            catch (const Rtx::Unsupported& obstacle)
            {
                Debug::getRawStdout() << obstacle.what() << '\n';
                return ExitStatus::Failed;
            }
        }

        /// What the renderer was handed at each place, without looking at what it drew. Walked twice: a second
        /// whole-graph walk should add nothing, and the report says what it added.
        ExitStatus commandScene(const Command& command)
        {
            return runHosted(command, planScene(command, frameFrom(command))).mExitStatus;
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
        ExitStatus commandShot(const Command& command)
        {
            ShotPlan plan = planShot(command, frameFrom(command));
            const std::filesystem::path& against = command.mLine.mAgainst;

            std::filesystem::create_directories(plan.mOut);
            if (const Result<void, std::string> checked = checkAgainst(plan.mOut, against); !checked.isOk())
                throw std::runtime_error(checked.error());

            plan.mRun.mRequest.mKeepsPictures = !against.empty();

            // Compared whatever the session answered, because a moved frame is what fails it, and
            // the run where something moved is the run whose tiles, dolls and sheets are wanted.
            const SessionResult ran = runHosted(command, std::move(plan.mRun));
            return worseOf(ran.mExitStatus, compareRuns(ran.mShelf, plan.mOut, against, plan.mPictures));
        }

        ExitStatus commandBench(const Command& command)
        {
            HostedRun run = planBench(command, frameFrom(command));

            const std::filesystem::path out = command.mLine.mOut.value_or(std::filesystem::path());
            if (!out.empty())
            {
                std::filesystem::create_directories(out);
                if (const Result<void, std::string> checked = checkAgainst(out, command.mLine.mAgainst);
                    !checked.isOk())
                    throw std::runtime_error(checked.error());
            }
            if (const std::filesystem::path& frameTimes = command.mLine.mBench.mFrameTimes; !frameTimes.empty())
                std::filesystem::create_directories(frameTimes);

            return runHosted(command, std::move(run)).mExitStatus;
        }

        /// A window on a place, with the game running behind it.
        ///
        /// The game and not a camera of this tool's own: a window is for seeing how something moves and whether an
        /// artefact is a still or a shimmer, which are questions about the frame a player gets. So the player flies
        /// it, with their own controls and console, in a body with every stat at 255, a Speed of 2000, level 255 and
        /// ten million gold, and the frame rate on the title. Collision comes off, because a view file's coordinates
        /// are where a camera stands rather than where a body fits.
        ExitStatus commandView(const Command& command)
        {
            return runHosted(command, planView(command, frameFrom(command))).mExitStatus;
        }

        /// Every claim the tree makes about what the renderer is handed and what it draws, asked of a real game at
        /// each place of a suite: a claim proved against a world of this tool's own would be about a world nobody
        /// plays.
        ExitStatus commandCheck(const Command& command)
        {
            HostedRun run = planCheck(command, frameFrom(command));
            std::filesystem::create_directories(outOf(command));
            return runHosted(command, std::move(run)).mExitStatus;
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
        ExitStatus commandNoise(const Command& command)
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

            HostedRun hosted = sessionFor(command, std::move(framed), std::move(plan.mStops));
            SessionRequest& request = hosted.mRequest;
            request.mSuite = run.mSuite;
            request.mKeepsPictures = true;

            // An earlier run's record goes before this one draws, so a run that ends before it judges
            // leaves none to be read as its own.
            const std::filesystem::path record = folder / sNoiseRecord;
            std::filesystem::remove(record);

            const SessionResult ran = runHosted(command, std::move(hosted));
            if (ran.mExitStatus != ExitStatus::Passed)
                return ran.mExitStatus;
            // A run that passed made its renderer, which noted the extents it traced at.
            assert(ran.mTraced.has_value());
            const PictureShelf& drawn = ran.mShelf;

            // What the session settled the bars' stops to, from the extents the run was traced at.
            const std::uint32_t barFrames = plan.mBar.under(*ran.mTraced);
            std::array<std::vector<NoiseFigures>, 2> measured;
            ExitStatus judged = judgeNoise(drawn, folder, plan.mSides, barFrames, measured[0]);
            if (versus.has_value())
            {
                Debug::getRawStdout() << std::format("versus --{}{}{}\n", asked,
                    plan.mOwnBar ? ", against a bar of its own" : "",
                    plan.mOwnReference ? " and a reference of its own" : "");
                judged = worseOf(judged, judgeNoise(drawn, folder, plan.mVersusSides, barFrames, measured[1]));
            }

            const std::span<const std::vector<NoiseFigures>> written(measured.data(), versus.has_value() ? 2 : 1);
            if (const Result<void, std::string> wrote = writeNoiseRecord(record, written); !wrote.isOk())
            {
                Debug::getRawStdout() << wrote.error() << '\n';
                return ExitStatus::Failed;
            }
            return judged;
        }

        /// A film of the keys a window wrote: every take drawn headless, its frames numbered through the film, then
        /// encoded. The plan comes first, whatever follows: a film is an hour of rendering, and a key too far away
        /// shows in the lengths before a frame is drawn. Stepped at the film's own rate, settled and natively
        /// reconstructed (`sFilmUpscale`), with the layers off unless named: the world moves a frame's worth between
        /// frames, no cell arrives on screen, and every pixel is traced.
        ExitStatus commandFilm(const Command& command)
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
                return ExitStatus::Passed;

            const std::filesystem::path directory = outOf(command);
            const std::filesystem::path frames = directory / "frames";
            std::filesystem::create_directories(frames);
            if (const std::size_t cleared = clearFrames(frames); cleared > 0)
                Debug::getRawStdout() << std::format("cleared {} frames of the last film\n", cleared);

            if (const ExitStatus status
                = runHosted(command, sessionFor(command, std::move(framed), stopsFor(plan, frames))).mExitStatus;
                status != ExitStatus::Passed)
                return status;

            std::filesystem::path video = directory / keys.stem();
            video += ".mp4";
            const std::string encode = encodeCommand(frames, video, plan.getFrames(), pacing.getRate());
            if (!line.mEncode)
            {
                Debug::getRawStdout() << "\nthe frames are in " << Files::pathToUnicodeString(frames)
                                      << "; to encode them:\n"
                                      << encode << '\n';
                return ExitStatus::Passed;
            }

            Debug::getRawStdout() << "\nencoding: " << encode << '\n' << std::flush;
            if (!Platform::Process::runShell(encode).succeeded())
            {
                Debug::getRawStdout() << "the encoder failed; the frames are in " << Files::pathToUnicodeString(frames)
                                      << '\n';
                return ExitStatus::Failed;
            }

            Debug::getRawStdout() << "the film is " << Files::pathToUnicodeString(video) << '\n';
            return ExitStatus::Passed;
        }

        /// A tour of the content that loaded: its route planned where the world stands, printed, and flown leg by
        /// leg, or nothing flown under `--plan`.
        ExitStatus commandTour(const Command& command)
        {
            return runHosted(command, planTourRun(command, frameFrom(command), readWorldMap)).mExitStatus;
        }

        /// One command: the line `--help` prints for it, and what it does. Identified by enum and not by name, because
        /// `verbs.hpp` holds the names and an option says which commands read it in the same terms, so a command whose
        /// options nothing reaches cannot drift in.
        struct CommandRow
        {
            Verb mVerb;
            std::string_view mSummary;
            ExitStatus (*mRun)(const Command&);
        };

        /// Every command there is, in the order `--help` prints them and the enum counts them. One list and not two, so
        /// a verb cannot be a command nobody can find or a line of help nothing answers.
        constexpr std::array<CommandRow, static_cast<std::size_t>(Verb::Count)> sCommands{
            CommandRow{ Verb::Info, "report the device this renderer would run on", commandInfo },
            CommandRow{ Verb::Scene, "read a place and report what the renderer would be handed", commandScene },
            CommandRow{ Verb::Shot,
                "the pictures of a place, with no window: the frame, a doll, a map tile, a texture sheet",
                commandShot },
            CommandRow{ Verb::View, "open a window on a place and fly around it", commandView },
            CommandRow{ Verb::Bench, "time a run of frames at each place of a suite", commandBench },
            CommandRow{ Verb::Check, "assert what the renderer is handed and what it draws, at every place of a suite",
                commandCheck },
            CommandRow{ Verb::Film, "fly through the keys `view --keys` wrote, into frames and a video", commandFilm },
            CommandRow{ Verb::Noise,
                "how noisy the frame is against 16 frames averaged, and how biased, at every place of a suite",
                commandNoise },
            CommandRow{ Verb::Tour,
                "fly over every land cell the content names and through its largest rooms, and report every fault met "
                "where it was met",
                commandTour },
        };

        static_assert(
            [] {
                for (std::size_t at = 0; at < sCommands.size(); ++at)
                    if (static_cast<std::size_t>(sCommands[at].mVerb) != at)
                        return false;
                return true;
            }(),
            "a command out of the enum's order");

        void printUsage(const bpo::options_description& options)
        {
            Debug::getRawStdout() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                                     "Usage: openmw-rtxtool <command> [options]\n\n"
                                     "Commands:\n";

            for (const CommandRow& row : sCommands)
                Debug::getRawStdout() << std::format("  {:<8} {}\n", verbName(row.mVerb), row.mSummary);

            Debug::getRawStdout()
                << "\nWith no arguments at all: a window on the ship at Seyda Neen, where the game starts.\n"
                   "The player's configuration is read and never written: what the engine saves on its way\n"
                   "out -- its settings, its log, its key bindings, its Lua storage -- goes to a directory of\n"
                   "this tool's own under the cache path.\n\n"
                << options;
        }

        ExitStatus dispatch(int argc, char* argv[])
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
                return ExitStatus::Passed;
            }

            Files::ConfigurationManager config;

            // Before the chain is walked, because this is the directory the engine writes into
            // (`ownConfigDirectory`); the log below lands there too.
            const std::filesystem::path own = ownConfigDirectory(config, nameThisRun());
            sweepAllButLiveRuns(own.parent_path());
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

            const std::optional<Verb> chosen = verbNamed(command);
            if (!chosen.has_value())
            {
                Debug::getRawStdout() << "Unknown command: " << command << "\n\n";
                printUsage(options.mDescription);
                return ExitStatus::Refused;
            }

            // Before the command runs: every option is declared on one description, so a command took every one and
            // read those it knew, and `shot --views=balmora` rendered the default view without a word.
            if (const std::string complaint = options.complainAbout(line, *chosen); !complaint.empty())
            {
                Debug::getRawStdout() << complaint;
                return ExitStatus::Refused;
            }

            // **The line read once, into what each option means**, once the configuration filled
            // what it left: every verb reads this, and none looks an option up by its name.
            const ToolLine read = options.read(variables, *chosen);

            // **Before any verb makes a device, because the driver reads where its cache is once.**
            // A cache of the shaders this run reads and of nothing else (`DriverCache`).
            const HarnessShaders shaders(*chosen, read.mShaderSource);
            const DriverCache driverCache(harnessDirectory(), shaders.getName(), shaders.getSet().mDigest);
            driverCache.applyToDriver();
            driverCache.sweep();

            // Read once for the run, which every framed verb asks three or more of.
            const ShippedDefaults shipped(config);
            return sCommands[static_cast<std::size_t>(*chosen)].mRun(
                Command{ read, config, own, shaders.getSet(), *chosen, shipped });
        }

        int run(int argc, char* argv[])
        {
            // Failures are reported here rather than left to `Debug::wrapApplication`, which puts up
            // an SDL message box when stdin is not a terminal. This tool is meant to be usable over
            // ssh and from a script, where a dialog nobody can see is a hang.
            try
            {
                return codeOf(dispatch(argc, argv));
            }
            catch (const bpo::error& e)
            {
                Debug::getRawStderr() << "openmw-rtxtool: " << e.what() << '\n';
                return codeOf(ExitStatus::Refused);
            }
            catch (const std::exception& e)
            {
                Debug::getRawStderr() << "openmw-rtxtool: " << e.what() << '\n';
                return codeOf(ExitStatus::Failed);
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
