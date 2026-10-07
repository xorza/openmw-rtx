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
#include <system_error>
#include <utility>
#include <vector>

#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/rtxsettings.hpp>
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
#include <components/rtx/environment/frameworld.hpp>
#include <components/rtx/environment/skylight.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/rtxvulkan/createrenderer.hpp>
#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>

#include "compare.hpp"
#include "film.hpp"
#include "harnessfolder.hpp"
#include "instruments/drivercache.hpp"
#include "model/benchrecord.hpp"
#include "model/benchrun.hpp"
#include "model/benchspec.hpp"
#include "model/blockfile.hpp"
#include "model/maprules.hpp"
#include "model/wholenumber.hpp"
#include "options.hpp"
#include "run.hpp"
#include "verbs.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        constexpr std::string_view applicationName = "RtxTool";

        /// Opens the log, loads the settings and hands the crash catcher what it reads of both: the
        /// version every report carries and how long without a frame is a hang. The game's own
        /// sequence, `parseOptions` in `apps/openmw/main.cpp`, restated, so a hang in the harness is
        /// reported as one in the game is.
        void startLogAndSettings(const Files::ConfigurationManager& config)
        {
            Debug::setupLogging(config.getLogPath(), applicationName);
            Debug::setCrashReports(config.getUserDataPath());
            Log(Debug::Info) << Version::getOpenmwVersionDescription();
            Crash::annotate("version", Version::getOpenmwVersionDescription());

            Settings::Manager::load(config);
            Crash::setHangLimit(std::chrono::seconds(Settings::general().mCrashHangSeconds));
        }

        /// Which layers a run wants, from what the command line asked for.
        ///
        /// Shared because `info` and every other command have to agree: a device that reported its
        /// limits under one set of layers and traced under another would be describing something
        /// nobody ran.
        Rtx::ValidationOptions validationFrom(const bpo::variables_map& variables)
        {
            // A level somebody typed is demanded: a run that asked for the layers and cannot have
            // them fails naming what is missing, because an empty log reads as a clean pass, while
            // a build that turned them on by default only warns.
            return Rtx::ValidationOptions{
                .mLevel
                = Rtx::sValidationNames.require(variables["validation"].as<std::string>(), "a validation level"),
                .mDemanded = !variables["validation"].defaulted(),
            };
        }

        /// Reports go to the unprefixed stream.
        ///
        /// `Debug::wrapApplication` routes `std::cout` through the log formatter, which stamps every
        /// line with a time and a level. That is right for a game and wrong for a tool whose output
        /// is meant to be read, diffed, or piped into something that parses it.
        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        /// What `--size` names.
        struct Size
        {
            std::uint32_t mWidth = 0;
            std::uint32_t mHeight = 0;
        };

        /// Parses `WIDTHxHEIGHT`.
        Size parseSize(std::string_view text)
        {
            const std::size_t cross = text.find('x');
            const std::optional<std::uint32_t> width
                = cross != std::string_view::npos ? wholeNumber<std::uint32_t>(text.substr(0, cross)) : std::nullopt;
            const std::optional<std::uint32_t> height
                = cross != std::string_view::npos ? wholeNumber<std::uint32_t>(text.substr(cross + 1)) : std::nullopt;

            if (!width.has_value() || !height.has_value() || *width == 0 || *height == 0)
                throw std::runtime_error("not a size: " + std::string(text));

            return Size{ .mWidth = *width, .mHeight = *height };
        }

        /// What `--exposure` asked for: a number to hold it at, or nothing to measure it.
        Rtx::ExposureRule parseExposure(std::string_view text)
        {
            // **Settled at every stop**, where the game opens from a day: a stop starts with no past
            // and warms up for less time than an eye takes to open in a room, and a measured picture
            // is of the eye that place settles on.
            if (text == "auto")
                return Rtx::MeasuredExposure{ .mStart = Rtx::EyeStart::Settled };

            const std::optional<float> value = parseFloat(text);
            if (!value.has_value() || !(*value > 0.0f))
                throw std::runtime_error("not an exposure: " + std::string(text));

            return Rtx::FixedExposure{ *value };
        }

        /// The layers a run that will be measured or compared gets, which is none unless it asked.
        ///
        /// **Off unless somebody asked, whatever the build default is.** The layers cost between a
        /// tenth and half the frame rate, and a profiling run that quietly measured one under
        /// instrumentation is worse than no run at all: it produces a number, and the number is
        /// wrong. GPU-assisted validation instruments every shader besides, so a picture drawn under
        /// one is not the picture the next run will be compared against.
        Rtx::ValidationOptions validationForMeasuring(const bpo::variables_map& variables)
        {
            return variables["validation"].defaulted() ? Rtx::ValidationOptions{} : validationFrom(variables);
        }

        /// What `--hour` named, or nothing where it was left at its default. `stopFor` is the rule
        /// this feeds.
        std::optional<float> hourGiven(const bpo::variables_map& variables)
        {
            if (variables["hour"].defaulted())
                return std::nullopt;

            const float hour = variables["hour"].as<float>();
            if (const Misc::Result<void, std::string_view> checked = checkHour(hour); !checked.isOk())
                throw std::runtime_error(std::format("--hour={} {}", hour, checked.error()));

            return hour;
        }

        /// The weather the line names, as `Rtx::weatherIndex` numbers it, read as a view file's is;
        /// refused with the option that named it where it is none of the ten.
        std::uint32_t weatherNamed(const std::string_view option, const std::string_view weather)
        {
            const std::optional<std::uint32_t> named = Rtx::weatherIndex(weather);
            if (!named.has_value())
                throw std::runtime_error(
                    std::format("--{}: \"{}\" {}: {}", option, weather, checkWeather(weather).error(), listWeathers()));
            return *named;
        }

        /// What `--turn-weather` named, in its order.
        std::vector<std::uint32_t> weathersToTurn(const bpo::variables_map& variables)
        {
            std::vector<std::uint32_t> turn;
            for (const std::string& weather : splitNames(variables["turn-weather"].as<std::string>()))
                turn.push_back(weatherNamed("turn-weather", weather));
            return turn;
        }

        /// What `--weather` named, or nothing where it was left at its default.
        std::optional<std::uint32_t> weatherGiven(const bpo::variables_map& variables)
        {
            if (variables["weather"].defaulted())
                return std::nullopt;

            return weatherNamed("weather", variables["weather"].as<std::string>());
        }

        /// What `--air` named, or nothing where the line names none. Text that names no air is
        /// refused, quoted whole.
        std::optional<Rtx::AirClock> airGiven(const bpo::variables_map& variables)
        {
            const std::string& text = variables["air"].as<std::string>();
            if (text.empty())
                return std::nullopt;

            const std::optional<Rtx::AirClock> air = parseAir(text);
            if (!air.has_value())
                throw std::runtime_error(
                    std::format("--air is not the sky's seconds from nought, the deck's scroll "
                                "from nought up to but not including four, and the drift's two "
                                "coordinates, separated by commas: \"{}\"",
                        text));

            return air;
        }

        /// The sky the line names for every place it stages: `stopFor` is the rule it feeds.
        StopSky skyGiven(const bpo::variables_map& variables, const Framed& framed)
        {
            return StopSky{
                .mHour = hourGiven(variables),
                .mDay = framed.mDay,
                .mWeather = weatherGiven(variables),
                .mAir = airGiven(variables),
            };
        }

        /// The point `--<name>` names, or nothing where the line names none. Text that names no point
        /// is refused, quoted whole.
        std::optional<osg::Vec3f> pointGiven(const bpo::variables_map& variables, const char* name)
        {
            const std::string& text = variables[name].as<std::string>();
            if (text.empty())
                return std::nullopt;

            const std::optional<osg::Vec3f> point = parseVec3(text);
            if (!point.has_value())
                throw std::runtime_error(
                    std::format("--{} is not three numbers separated by commas: \"{}\"", name, text));

            return point;
        }

        /// Every view a run names, settled: a condition named on the command line is every
        /// place's, and none of them keeps its own.
        std::vector<Stop> stopsFrom(
            const std::vector<Stop>& views, const bpo::variables_map& variables, const Framed& framed)
        {
            const StopSky given = skyGiven(variables, framed);

            std::vector<Stop> stops;
            stops.reserve(views.size());
            for (const Stop& view : views)
                stops.push_back(stopFor(view, given));

            return stops;
        }

        /// What every command is handed: the line it was given, the configuration that line was
        /// read against, where the resources are, and which of their shader sets a renderer reads —
        /// the one the driver's cache was pointed at.
        struct Command
        {
            const bpo::variables_map& mVariables;
            const ToolOptions& mOptions;
            Files::ConfigurationManager& mConfig;
            const std::filesystem::path& mResources;
            const Rtx::ShaderSet& mShaders;
            Verbs mVerb;
        };

        /// The places a run can visit, in the harness's folder.
        std::filesystem::path viewsFile()
        {
            return harnessDirectory() / "views.cfg";
        }

        /// The suites, each a list of places in `viewsFile`.
        std::filesystem::path suitesFile()
        {
            return harnessDirectory() / "benches.cfg";
        }

        /// Where a verb writes its pictures: `--out`, or a directory named for the verb.
        std::filesystem::path outOf(const Command& command)
        {
            const bpo::variable_value& out = command.mVariables["out"];
            return out.defaulted() ? std::filesystem::path(verbName(command.mVerb))
                                   : std::filesystem::path(out.as<std::string>());
        }

        /// The whole of a `Framed`, from the command line: the window's settings and the
        /// renderer's setup, read once, so that no verb splits the line again by hand.
        Framed frameFrom(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            const Size size = parseSize(variables["size"].as<std::string>());

            // **A window is the played game with the walls off, so what the player set stands
            // unless an option was typed over it.** Every other command has to state its frame,
            // so that two runs of it are one run whatever a settings file says — which is what
            // the shipped defaults are for, and not the player's value of the same setting. A
            // window that took them stood four cells of ground under a player who had set eight,
            // and upscaled at a quality they had not.
            const bool watched = command.mVerb == Verbs::View;
            const auto given
                = [&](const char* name) { return variables.count(name) != 0 && !variables[name].defaulted(); };
            const auto typed = [&](const char* name) { return !watched || given(name); };

            Framed framed;
            framed.mWindow.mWidth = size.mWidth;
            framed.mWindow.mHeight = size.mHeight;
            framed.mWindow.mFieldOfView = variables["fov"].as<float>();
            if (watched)
                framed.mWindow.keepPlayersPacing();
            framed.mDay = variables["day"].as<int>();
            if (const Misc::Result<void, std::string_view> checked = checkDay(framed.mDay); !checked.isOk())
                throw std::runtime_error(std::format("--day={} {}", framed.mDay, checked.error()));

            const auto spelled
                = [&](const char* name) -> std::string_view { return variables[name].as<std::string>(); };

            framed.mMaps = mapRulesFrom(variables, command.mVerb);

            // **The settings the ray tracer reads, from the harness's own sources and through the
            // game's one derivation.** Given on the line, the line's; a window's, the player's; a
            // measured run's, the file's default, every one of them — but for the upscaler, whose
            // default for a run is the harness's own (`sUpscaleByDefault`). So two machines that
            // differ only in their `settings.cfg` measure one scene under one line.
            const bool grass = given("grass") ? variables["grass"].as<bool>()
                : watched                     ? Settings::groundcover().mEnabled.get()
                                              : shippedDefault<bool>(command.mConfig, "Groundcover", "enabled");

            // The world reads the groundcover files only where `[Groundcover] enabled` says
            // (`World::loadGroundcoverFiles`), so the run's answer is written where the world reads
            // it, as the companion maps' rules are.
            Settings::groundcover().mEnabled.set(grass);

            // What a `_spec` map's channels mean under the rules the line named, or the player's.
            const Rtx::SpecularLayout specularLayout = [&] {
                if (!framed.mMaps.has_value())
                    return Settings::rtx().mSpecularMapLayout.get();
                switch (*framed.mMaps)
                {
                    case MapRules::Shipped:
                        return Rtx::sSpecularLayoutNames.require(
                            shippedDefault<std::string>(command.mConfig, "RTX", "specular map layout"),
                            "a specular map layout");
                    case MapRules::Classic:
                        return Rtx::SpecularLayout::Classic;
                    case MapRules::MetalRoughness:
                        return Rtx::SpecularLayout::MetalRoughness;
                }
                Crash::fatal("map rules with no name");
            }();

            const MWRender::RtxSettings derived = MWRender::RtxSettings::derive(MWRender::RtxSettingValues{
                .mUpscale = typed("upscale") ? Rtx::sUpscaleNames.require(spelled("upscale"), "an upscale mode")
                                             : Settings::rtx().mUpscale.get(),
                .mDistantLandCells = given("distant-cells") ? variables["distant-cells"].as<float>()
                    : watched                               ? Settings::rtx().mDistantLandCells.get()
                              : shippedDefault<float>(command.mConfig, "RTX", "distant land cells"),
                .mViewingDistance = watched ? Settings::camera().mViewingDistance.get()
                                            : shippedDefault<float>(command.mConfig, "Camera", "viewing distance"),
                .mObjectPaging = given("distant-statics") ? variables["distant-statics"].as<bool>()
                    : watched                             ? Settings::terrain().mObjectPaging.get()
                              : shippedDefault<bool>(command.mConfig, "Terrain", "object paging"),
                .mObjectPagingMinSize = watched
                    ? Settings::terrain().mObjectPagingMinSize.get()
                    : shippedDefault<float>(command.mConfig, "Terrain", "object paging min size"),
                .mGroundcover = grass,
                .mGroundcoverDistance = watched
                    ? Settings::groundcover().mRenderingDistance.get()
                    : shippedDefault<float>(command.mConfig, "Groundcover", "rendering distance"),
                .mGroundcoverDensity = watched ? Settings::groundcover().mDensity.get()
                                               : shippedDefault<float>(command.mConfig, "Groundcover", "density"),
                .mGroundcoverPointLighting = watched
                    ? Settings::groundcover().mPointLighting.get()
                    : shippedDefault<bool>(command.mConfig, "Groundcover", "point lighting"),
                .mSpecularMapLayout = specularLayout,
                .mIndirectLight = given("indirect") ? spelled("indirect")
                    : watched                       ? Settings::rtx().mIndirectLight.get()
                              : shippedDefault<std::string>(command.mConfig, "RTX", "indirect light"),
                .mAnisotropy = watched ? Settings::general().mAnisotropy.get()
                                       : shippedDefault<int>(command.mConfig, "General", "anisotropy"),
                .mGamma = given("gamma") ? variables["gamma"].as<float>()
                    : watched            ? Settings::video().mGamma.get()
                                         : shippedDefault<float>(command.mConfig, "Video", "gamma"),
                .mLitEnvironmentMaps = watched
                    ? Settings::shaders().mApplyLightingToEnvironmentMaps.get()
                    : shippedDefault<bool>(command.mConfig, "Shaders", "apply lighting to environment maps"),
            });
            framed.mSetup.mMirror = derived.mMirror;

            // **The layers the command's row says, unless the line names some**: `VerbPolicy`.
            framed.mSetup.mRun.mValidation
                = policyOf(command.mVerb).mMeasures ? validationForMeasuring(variables) : validationFrom(variables);
            framed.mSetup.mShaders = command.mShaders;
            if (variables.count("memory-budget") != 0)
                framed.mSetup.mRun.mMemoryBudget = variables["memory-budget"].as<std::uint64_t>() * 1024 * 1024;

            Rtx::RenderProfile& profile = framed.mSetup.mRun.mProfile;
            profile.mUpscale = derived.mUpscale;
            profile.mAnisotropy = derived.mAnisotropy;
            profile.mGamma = derived.mGamma;
            profile.mLitEnvironmentMaps = derived.mLitEnvironmentMaps;
            profile.mReconstruction.mIndirect = derived.mIndirect;
            profile.mDelight = variables["delight"].as<float>();
            profile.mShow = Rtx::sSurfaceViewNames.require(variables["show"].as<std::string>(), "a surface view");
            profile.mExposure = parseExposure(variables["exposure"].as<std::string>());
            profile.mStressOverlapMs = variables["hold"].as<bool>() ? sCheckHoldMs : 0.0;
            profile.mSpecializeLaunches = variables["variants"].as<bool>();
            readReconstruction(variables, profile.mReconstruction);

            return framed;
        }

        int runInfo(const Command& command, const Rtx::ValidationOptions& validation)
        {
            // A one-pixel target: this reports on a device rather than drawing with it, and the
            // default would spend fifty megabytes of images to print a page of text.
            //
            // **The shaders are still named, because standing a renderer up compiles one.**
            // Reporting on a device is not a reason to build half a renderer, and a build whose
            // shaders are missing should say so here rather than at the first frame asked for.
            //
            // **And no pipeline cache, as no verb of this tool keeps one**: `RtxRenderer` says why a
            // measured run compiles from source, and this verb's few seconds are that compile.
            try
            {
                const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createVulkanRenderer(Rtx::RendererOptions{
                    .mShaders = command.mShaders,
                    .mWidth = 1,
                    .mHeight = 1,
                    .mRun = { .mValidation = validation },
                });
                out() << renderer->describeDevice();
                return 0;
            }
            catch (const Rtx::Unsupported& obstacle)
            {
                out() << obstacle.what() << '\n';
                return 1;
            }
        }

        /// Where someone starts when they have said nothing about where: the ship at Seyda Neen,
        /// where the game starts and the one place every player of it has stood.
        constexpr std::string_view sDefaultView = "seyda-neen-ship";

        /// Whether a run starts from a savegame, which is then what says where the player stands
        /// and what hour and weather it is — unless the line names a view or a cell over it.
        bool startsFromSave(const bpo::variables_map& variables)
        {
            return !variables["load-savegame"].as<Files::MaybeQuotedPath>().empty();
        }

        /// The view a run names, or null where it named none and gave a cell instead.
        const Stop* findChosenView(const bpo::variables_map& variables, std::vector<Stop>& views)
        {
            std::string name = variables["view"].as<std::string>();
            if (name.empty())
            {
                if (!variables["cell"].as<std::string>().empty() || startsFromSave(variables))
                    return nullptr;

                name = sDefaultView;
            }

            views = loadViews(viewsFile());
            return &requireView(views, name);
        }

        /// What a `shot` writes its frames' hashes to, beside the pictures, and reads a reference's
        /// from.
        constexpr std::string_view sShotHashes = "hashes.csv";

        /// Runs `stop` for `frames` once the world stood whole and its histories converged over
        /// `sHistoryFrames`, so its pictures are the ones a player standing there sees. Still where
        /// the command's row freezes the world (`VerbPolicy::mFreezes`), which `sessionFor` applies.
        ///
        /// @param frames how many to measure once the world has arrived. Why a command wants more
        ///        than one is that command's to say.
        void measureFrames(Stop& stop, const std::uint32_t frames = 1)
        {
            stop.mSchedule.mSpec = BenchSpec{ .mRun = { .mFrames = frames }, .mWarm = { .mFrames = sHistoryFrames } };
        }

        /// What `policy` does to one place: the route and the track a command does not follow go,
        /// the clock stops where the row freezes and nothing is flown, and every frame is hashed
        /// where the row hashes.
        ///
        /// **Frozen is what still means.** The world does not step, so what one frame differs from
        /// the next by is the renderer and nothing else — which is what a picture, a digest and a
        /// pixel comparison are each about. A view's route is flown with the clock going, because a
        /// camera crossing a stopped world measures the streaming and nothing that lives in it. A
        /// route that holds the world is the command's own staging and not a view's, and the row
        /// leaves it as it is.
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
            const bpo::variables_map& variables = command.mVariables;

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
            request.mHud = variables["hud"].as<bool>();
            request.mSetup.mInterface = request.mPlayed || request.mHud;
            request.mVanity = variables["vanity"].as<bool>();
            request.mNightEye = variables["night-eye"].as<int>();
            request.mRandomSeed = variables["random-seed"].as<unsigned int>();

            return request;
        }

        /// Runs a list of stops against a real game, which is what every command that writes
        /// pictures or reports does.
        int runStops(const Command& command, const Framed& framed, std::vector<Stop> stops)
        {
            return runHosted(command.mVariables, command.mConfig, command.mResources, framed.mWindow,
                sessionFor(command, framed, std::move(stops)));
        }

        /// How long every stop of a run lasts, from what the command line asked for.
        ///
        /// **Frames win over seconds where both were named.** `--frames` is what a run that has to
        /// be exactly reproducible asks for, and `--seconds` is what a run being read asks for.
        BenchSpec specFrom(const bpo::variables_map& variables)
        {
            BenchSpec spec;
            spec.mRun = variables["frames"].as<std::uint32_t>() > 0
                ? BenchSpan{ .mFrames = variables["frames"].as<std::uint32_t>() }
                : BenchSpan{ .mSeconds = variables["seconds"].as<float>() };
            spec.mWarm = BenchSpan{ .mSeconds = variables["warmup"].as<float>() };

            return spec;
        }

        /// The one place a command names on its line, as a stop: a view, a cell, a save, and
        /// whatever the line says over them.
        Stop stageOnePlace(const Command& command, const Framed& framed)
        {
            const bpo::variables_map& variables = command.mVariables;

            // Holds what the view below points into, for as long as this function needs it.
            std::vector<Stop> views;
            const Stop* found = findChosenView(variables, views);
            const std::string cell = variables["cell"].as<std::string>();

            // **A save is the place, unless the line names one over it.** The stop then stands
            // where the save left the player, at the save's hour, day and weather, and only what
            // the line names is changed — where `stopFor` would stand it at noon under a clear sky
            // on the first day, which is a view's rule and not a save's.
            Stop staged;
            if (found == nullptr && cell.empty() && startsFromSave(variables))
            {
                staged.mName
                    = Files::pathToUnicodeString(variables["load-savegame"].as<Files::MaybeQuotedPath>().stem());
                staged.mSky.mHour = hourGiven(variables);
                staged.mSky.mWeather = weatherGiven(variables);
                staged.mSky.mAir = airGiven(variables);
                if (!variables["day"].defaulted())
                    staged.mSky.mDay = framed.mDay;
            }
            else
            {
                const Stop view = found != nullptr ? *found : Stop{ .mStand = { .mCell = cell } };
                staged = stopFor(view, skyGiven(variables, framed));
            }

            // Anything given on the command line wins over the view, which is the rule `stopFor`
            // already follows for the hour and the sky.
            if (const std::optional<osg::Vec3f> origin = pointGiven(variables, "pos"))
                staged.mStand.mEye = origin;

            if (const std::optional<osg::Vec3f> target = pointGiven(variables, "look");
                target.has_value() && !staged.mStand.lookAt(*target))
                throw std::runtime_error(
                    "--look faces the camera from an eye, and neither --pos nor the view names one");

            return staged;
        }

        /// The places a picture or a report is made at, each held still: the views `--views` names,
        /// or the one place the line names where it names none.
        ///
        /// **Held still here and not by each command**, so a picture the world moved under cannot
        /// come out of a command that forgot the freeze, with nothing in the output to say so.
        ///
        /// @param frames how many to measure at each place once the world has arrived. Why a
        ///        command wants more than one is that command's to say.
        std::vector<Stop> stagePlaces(const Command& command, const Framed& framed, const std::uint32_t frames)
        {
            const bpo::variables_map& variables = command.mVariables;
            const std::string named = variables["views"].as<std::string>();

            std::vector<Stop> stops;
            if (named.empty())
                stops.push_back(stageOnePlace(command, framed));
            else
            {
                stops = stopsFrom(chooseViews(loadViews(viewsFile()), splitNames(named)), variables, framed);
            }

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

        /// **`--views` beats `--suite`, and both name entries in `views.cfg`.** A suite is a list
        /// written down so a run can be repeated without remembering it; a list on the command line
        /// is the same thing for one run. Neither carries coordinates: those live with the view, so
        /// the frame a picture is taken of and the frame a number is measured on stay one frame.
        ///
        /// @param ownSuite the suite the verb runs when neither `--views` nor `--suite` names one:
        ///        `bench` measures the frame budget's places and `check` walks a route as well.
        SuiteRun chooseBenchViews(const bpo::variables_map& variables, const std::string_view ownSuite)
        {
            const std::vector<Stop> views = loadViews(viewsFile());
            const std::string named = variables["views"].as<std::string>();

            SuiteRun run;
            std::vector<std::string> wanted;
            if (named.empty())
            {
                run.mSuite
                    = variables["suite"].defaulted() ? std::string(ownSuite) : variables["suite"].as<std::string>();

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
                wanted = splitNames(named);

            run.mViews = chooseViews(views, wanted);
            return run;
        }

        int runListViews()
        {
            for (const Stop& view : loadViews(viewsFile()))
            {
                out() << "  " << view.mName << "\n      " << view.mStand.mCell;

                // A place that fixes a condition is a different frame from the same camera at noon
                // under a clear sky, and this listing is how a view is found.
                if (view.mSky.mHour.has_value())
                    out() << " at " << describeHour(*view.mSky.mHour);

                if (view.mSky.mWeather.has_value())
                    out() << " in " << *view.mSky.mWeather;

                out() << "\n      " << view.mNote << '\n';
            }

            return 0;
        }

        int commandInfo(const Command& command)
        {
            if (command.mVariables["folders"].as<bool>())
            {
                const Files::FixedPath<> game("openmw");
                out() << "config " << Files::pathToUnicodeString(game.getUserConfigPath()) << '\n'
                      << "data " << Files::pathToUnicodeString(game.getUserDataPath()) << '\n';
                return 0;
            }

            const Rtx::ValidationOptions validation = validationFrom(command.mVariables);

            return runInfo(command, validation);
        }

        /// What the renderer was handed at each place, without looking at what it drew.
        ///
        /// **Walked twice, always.** A second whole-graph walk should add nothing, and the report
        /// says what it added; a run that has read a cell is the cheapest place to ask.
        int commandScene(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            const Framed framed = frameFrom(command);

            std::vector<Stop> stops = stagePlaces(command, framed, 1);
            for (Stop& stop : stops)
            {
                stop.mActions.mFind = variables["find"].as<std::string>();
                stop.mActions.mDigest = stop.mActions.mFind.empty();
                stop.mActions.mWalkTwice = true;
            }

            return runStops(command, framed, std::move(stops));
        }

        /// The pictures of each place, taken headless: the frame, and the doll, the tile and the
        /// sheet where asked for.
        ///
        /// **The world a player stands in and not one staged here**, which is the whole of what
        /// this path is for: the cells are read by `MWWorld::Scene`, the people are dressed by
        /// `NpcAnimation` and the sky is reported by `MWWorld::WeatherManager`, so the picture is
        /// the one the game draws rather than one derived beside it.
        ///
        /// **Every picture a stop can make, in one run**, because `Actions` holds them all and
        /// each verb that made one started an engine of its own for it. And every view, under
        /// `--views`, with `--against` saying which pictures a change moved.
        ///
        /// **The frame is judged by its hashes and not by its pixels.** Every frame of a stop is
        /// hashed the way a `bench --hashes` hashes one — the trace's own images, what the frame
        /// handed the reconstruction, the scene — into `hashes.csv` beside the pictures, and
        /// `--against` compares that table first. `FrameHashes` says why the picture past an
        /// upscaler cannot be the verdict; the tile, the doll and the sheet are traced
        /// without it and are compared as pictures.
        int commandShot(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            const Framed framed = frameFrom(command);

            const std::filesystem::path against = variables["against"].as<std::string>();

            // **Traced more than once, because one submit measures the clock and not the shader.**
            // A GPU idles at a fraction of its clock and ramps only under load, so the same frame
            // from a cold start times anywhere within a factor of several.
            //
            // **Accumulating replaces repeating rather than joining it.** A run that also honoured
            // the repeat default would quietly average eight frames more than it was asked for, and
            // a convergence ladder built on that reads as though the first frames bought nothing.
            const std::uint32_t accumulate = variables["accumulate"].as<std::uint32_t>();
            const std::uint32_t frames
                = accumulate > 0 ? accumulate : std::max(variables["repeat"].as<std::uint32_t>(), 1u);

            std::vector<Stop> stops = stagePlaces(command, framed, frames);

            const std::filesystem::path out = outOf(command);
            std::filesystem::create_directories(out);

            if (const Misc::Result<void, std::string> checked = checkAgainst(out, against); !checked.isOk())
                throw std::runtime_error(checked.error());

            // **What each picture is held to is where it came from.** A frame the wavelet composed and
            // nothing upscaled is the picture the hashes cannot judge; a doll and a tile are always
            // denoised (`Reconstruction::forPicture`); the sheet is the textures and nothing traced.
            const Rtx::RenderProfile& profile = framed.mSetup.mRun.mProfile;
            const PictureRule frameRule = profile.mReconstruction.mDenoise && !Rtx::upscales(profile.mUpscale)
                ? PictureRule::Denoised
                : PictureRule::Hashed;

            const std::string doll = variables["doll"].as<std::string>();
            std::vector<WrittenPicture> written;
            for (Stop& stop : stops)
            {
                stop.mSchedule.mAccumulate = accumulate;

                const auto file = [&](const std::string_view suffix, const PictureRule rule) {
                    written.push_back(
                        WrittenPicture{ .mFile = stop.mName + std::string(suffix) + ".png", .mRule = rule });
                    return out / written.back().mFile;
                };

                stop.mActions.mCapture = file("", frameRule);
                if (!doll.empty())
                    stop.mActions.mDoll = Actions::Doll{ .mWho = doll, .mFile = file("-doll", PictureRule::Denoised) };
                if (variables["map"].as<bool>())
                    stop.mActions.mMapTile = file("-map", PictureRule::Denoised);
                if (variables["textures"].as<bool>())
                    stop.mActions.mSheet = file("-textures", PictureRule::Exact);
            }

            clearPictures(out, written);

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mHashes = out / sShotHashes;
            if (!against.empty())
                request.mAgainst = against / sShotHashes;

            // Compared whatever the session answered, because a moved frame is what fails it, and
            // the run where something moved is the run whose tiles, dolls and sheets are wanted.
            const int status
                = runHosted(variables, command.mConfig, command.mResources, framed.mWindow, std::move(request));
            const int compared = compareRuns(out, against, written);
            return status != 0 ? status : compared;
        }

        int commandBench(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            Framed framed = frameFrom(command);

            // A bench draws frames the way a player sees them and sums none of them, so it is
            // measured at the width the game runs at. Every other verb keeps the reference's.
            framed.mSetup.mRun.mProfile.mRadianceWidth = Rtx::RadianceWidth::Shown;
            framed.mSetup.mHeadless = !variables["window"].as<bool>();

            const SuiteRun run = chooseBenchViews(variables, "default");
            std::vector<Stop> stops = stopsFrom(run.mViews, variables, framed);

            const BenchSpec spec = specFrom(variables);
            const std::vector<std::uint32_t> turn = weathersToTurn(variables);
            const bool hashing = !variables["hashes"].as<std::string>().empty()
                || !variables["against"].as<std::string>().empty() || !variables["pictures"].as<std::string>().empty();

            const std::filesystem::path frameTimes = variables["frame-times"].as<std::string>();
            if (!frameTimes.empty())
                std::filesystem::create_directories(frameTimes);

            const std::filesystem::path pictures = variables["pictures"].as<std::string>();
            if (!pictures.empty())
                std::filesystem::create_directories(pictures);

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
            request.mJson = variables["json"].as<std::string>();
            request.mHashes = variables["hashes"].as<std::string>();
            request.mAgainst = variables["against"].as<std::string>();
            request.mPictures = pictures;
            request.mPerfControl = variables["perf-control"].as<std::string>();
            request.mSetup.mSettled = run.mSettled;

            return runHosted(variables, command.mConfig, command.mResources, framed.mWindow, std::move(request));
        }

        /// A window on a place, with the game running behind it.
        ///
        /// **The game and not a camera of this tool's own.** What a window is for is seeing how
        /// something moves and whether an artefact is a still or a shimmer, and both are questions
        /// about the frame a player gets — so the player is who flies it, with their own controls,
        /// their own collision and their own console — in a body with every stat at 255, a Speed
        /// of 2000, level 255 and ten million gold, and the frame rate on the window's title.
        ///
        /// Collision comes off, because a view file's coordinates are where a camera stands rather
        /// than where a body fits.
        int commandView(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            Framed framed = frameFrom(command);

            // Watched and never summed, like a bench.
            framed.mSetup.mRun.mProfile.mRadianceWidth = Rtx::RadianceWidth::Shown;

            // **On the wall, because somebody is watching.** A stepped world runs as fast as the
            // card draws it, which at two hundred frames a second is three times over; a window
            // is the played game with the walls off, and the played game follows the wall.
            framed.mSetup.mHeadless = false;
            framed.mStep = std::nullopt;

            Stop staged = stageOnePlace(command, framed);

            // **A schedule with no end, because somebody is watching.** `--frames` closes it after
            // that many, which is how the window path gets exercised by something that cannot click.
            const std::uint32_t frames = variables["frames"].as<std::uint32_t>();
            staged.mSchedule.mSpec.mRun = BenchSpan{ .mFrames = frames > 0 ? frames : BenchSpan::sUntilClosed };

            std::vector<Stop> stops;
            stops.push_back(std::move(staged));

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mQuitAtEnd = frames > 0;
            request.mKeys = variables["keys"].as<std::string>();
            request.mHomePictures = outOf(command);

            return runHosted(variables, command.mConfig, command.mResources, framed.mWindow, std::move(request), true);
        }

        /// Every claim the tree makes about what the renderer is handed and what it draws, asked
        /// of a real game at each place of a suite.
        ///
        /// **These were tests against a world of this tool's own.** That world read its cells by
        /// hand, dressed its people by rules of its own and derived its sky from the content files,
        /// so a claim proved there was a claim about a world nobody plays. Asked here, each of them
        /// is about the world a player stands in.
        int commandCheck(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            Framed framed = frameFrom(command);
            if (variables["hold"].defaulted())
                framed.mSetup.mRun.mProfile.mStressOverlapMs = sCheckHoldMs;

            const SuiteRun run = chooseBenchViews(variables, "check");
            std::vector<Stop> stops = stopsFrom(run.mViews, variables, framed);

            const std::span<const Check> every = everyCheck();

            // **Every picture a stop can write, at the first place, because each leaves through a
            // path the frame's own passes never touch**: the capture adds the read back a picture
            // is written from, and the tile and the doll are offscreen traces with descriptor sets
            // and targets of their own. Under the layers, this is what finds a barrier they miss.
            const std::filesystem::path out = outOf(command);
            std::filesystem::create_directories(out);
            stops.front().mActions.mCapture = out / (stops.front().mName + ".png");
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
                    stop.mSchedule.mSpec.mRun = BenchSpan{ .mSeconds = variables["seconds"].as<float>() };
            }

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mSuite = run.mSuite;

            return runHosted(variables, command.mConfig, command.mResources, framed.mWindow, std::move(request));
        }

        /// How noisy the frame a player sees is, against sixteen frames averaged, and how far what it
        /// converges to stands from the truth, at each place of a suite: five pictures of each place,
        /// in one run, and a verdict on them.
        ///
        /// **The reference, the bar, the bar's limit, the frame and the frame's mean, in that
        /// order.** The reference averages
        /// `sNoiseReferenceFrames` frames traced unfiltered and jittered, from the white hash, which
        /// is a sequence neither of the others draws from, so it shares no sample with them, and at
        /// no texture level epsilon; its exposure is measured as a played frame's is, and it is
        /// written as the renderer summed it, at sixteen bits a channel and undithered
        /// (`Actions::mDeepCapture`), so no byte stands under the bias measured against it. The bar averages
        /// `sNoiseBarFrames` frames, unfiltered and as the run otherwise traces — or, flown in, as many as the frame's
        /// history could hold, `noiseBarFramesAfter`. The frame is the run's own, after the warm-up its history
        /// converges over, upscaled as the run is — or, with `--strafe` or `--walk`, after it flew into the place
        /// from the side or from behind (`Stand::approachFrom`). The reference and the bar are traced with no upscaler,
        /// at the frame's own output size, so an upscaled frame is held to the picture it stands for and not to another
        /// upscale of it. The bar's limit is `sNoiseMeanDraws` draws of the bar, and the frame's mean `sNoiseMeanDraws`
        /// draws of the frame, each its own stop over the same warm-up as the frame: what each converges to, drawn its
        /// own way. **Both means are of the pictures as shown**, the bytes averaged, so each is the centre of what its
        /// own pictures scatter around and the two spreads are measured alike; a mean of radiance mapped once is a
        /// centre the curve moved, and a frame's distance from it holds that move as noise. **Every stop at a place
        /// draws samples of its own** (`sNoiseSampleStride`), or the draws of a held world are one draw repeated. Every
        /// picture but the reference holds the exposure the reference ended on, so all are mapped by one curve and the
        /// scale is derived rather than stated.
        ///
        /// **With `--versus`, a second side at each place after the first**: its frame and the frame's
        /// mean at the first side's draws, and its own reference, bar and limit before them only
        /// where its unfiltered frames trace otherwise, so an A/B of a switch only the filters read
        /// traces the half of the run that cannot differ once. Each side is judged on its own lines.
        ///
        /// **Judged against the bar and not against a number**: a frame is as clean as the bar when it
        /// stands no further from its own mean than the bar from its limit, by the mean and at the 99th
        /// percentile — `judgeNoise` says why noise against noise. The bar is measured at the same
        /// place, so it carries the place's own difficulty with it.
        int commandNoise(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            const Framed framed = frameFrom(command);

            const SuiteRun run = chooseBenchViews(variables, "noise");
            const std::vector<Stop> places = stopsFrom(run.mViews, variables, framed);

            const std::filesystem::path folder = outOf(command);
            std::filesystem::create_directories(folder);

            const Rtx::ReconstructionRequest& played = framed.mSetup.mRun.mProfile.mReconstruction;

            // The other side, where the line names one: the frame and its mean again with one switch
            // of the reconstruction changed, at the same draws (`ToolOptions::versus`).
            const std::string asked = variables["versus"].as<std::string>();
            const std::optional<Rtx::ReconstructionRequest> versus
                = asked.empty() ? std::nullopt : std::optional(command.mOptions.versus(variables, played, asked));

            // **The reference and the bar trace unfiltered, every frame a draw of its own**
            // (`ReconstructionRequest::unfiltered`): a frame that reused the ones before it is not
            // one more sample of the truth, and neither is a frame of the bar. Their indirect light
            // stays the run's, since a traced bounce and none are two integrands. So the other side
            // traces its own only where its unfiltered frames trace otherwise.
            const bool ownBar = versus.has_value() && versus->unfiltered() != played.unfiltered();
            const auto referenceOf = [](const Rtx::ReconstructionRequest& side) {
                Rtx::ReconstructionRequest truth = side.unfiltered();
                truth.mJitter = true;
                truth.mNoise = Rtx::NoiseSource::WhiteHash;
                // **The truth reads every texture at the level its footprint asks**, whatever the
                // run's epsilon: an epsilon is a knob on the frame, and a reference that moved with
                // it would take the frame's softness for its own and report no bias at all.
                truth.mLevelEpsilon = 0.0f;
                // **And draws every source for its bit**: a floor rides a minor source's light on
                // another's shadow, which is the bias the A/B of the floor measures.
                truth.mShadowFloor = 0.0f;
                // And weighs every lamp, which a fixed count of candidates estimates.
                truth.mLampCandidates = 0u;
                return truth;
            };
            const Rtx::ExposureRule held = Rtx::HeldExposure{};

            // One picture of `place` after `frames` frames: their sum where `summed`, and the last of
            // them where not.
            const auto picture
                = [&](const Stop& place, const std::string_view suffix, const std::uint32_t frames, const bool summed,
                      const std::optional<Rtx::ReconstructionRequest>& reconstruction,
                      const std::optional<Rtx::ExposureRule>& exposure, const std::optional<Rtx::Upscale> upscale) {
                      Stop stop = place;
                      stop.mName += suffix;
                      measureFrames(stop, frames);
                      stop.mSchedule.mAccumulate = summed ? frames : 0;
                      stop.mSchedule.mReconstruction = reconstruction;
                      stop.mSchedule.mExposure = exposure;
                      stop.mSchedule.mUpscale = upscale;
                      stop.mActions.mCapture = folder / (stop.mName + ".png");
                      return stop;
                  };

            const float strafe = variables["strafe"].as<float>();
            const float walk = variables["walk"].as<float>();
            const bool flies = strafe > 0.0f || walk != 0.0f;

            // Each leg's frame is held to the samples a shown pixel its history could hold
            // (`noiseFrameFor`), standing as well.
            const Rtx::FrameExtents extents
                = Rtx::extentsFor(framed.mWindow.mWidth, framed.mWindow.mHeight, framed.mSetup.mRun.mProfile.mUpscale);
            const Misc::Result<NoiseFrame, std::string> taken
                = noiseFrameFor(variables["cut"].as<std::uint32_t>(), flies, extents);
            if (!taken.isOk())
                throw std::runtime_error(taken.error());
            const NoiseFrame& leg = taken.value();
            const std::uint32_t barFrames = leg.mBarFrames;

            // The frame's own stop, flying in where the line asks: a route that holds the world, so
            // the frame flies through the world the reference stands in (`applyPolicy`).
            const auto frame = [&](const Stop& place, const Rtx::ReconstructionRequest& side) {
                Stop stop = picture(place, "", flies ? sNoiseFlightFrames : 1, false, side, held, std::nullopt);
                if (leg.mWarmup.has_value())
                    stop.mSchedule.mSpec.mWarm = BenchSpan{ .mFrames = *leg.mWarmup };
                if (!flies)
                    return stop;

                if (!place.mStand.mEye.has_value())
                    throw std::runtime_error(std::format(
                        "--strafe and --walk need a place that names an eye, and {} names none", place.mName));

                // An eye that started past the point it faces would fly in facing backwards.
                if (walk < 0.0f
                    && -walk >= (place.mStand.getLook() - *place.mStand.mEye) * place.mStand.getLevelAhead())
                    throw std::runtime_error(
                        std::format("--walk={} starts past the point {} faces", walk, place.mName));

                Approach approach = stop.mStand.approachFrom(strafe, walk, worldStep(framed.mStep), sNoiseFlightFrames);
                stop.mStand = std::move(approach.mFrom);
                stop.mSchedule.mRoute = approach.mRoute;
                return stop;
            };

            // `sNoiseMeanDraws` draws of `drawn`, each adding its last frame to the mean `suffix`
            // names, as shown.
            const auto drawMean
                = [&](const Stop& place, const Stop& drawn, const std::string_view suffix, std::vector<Stop>& into) {
                      for (std::uint32_t draw = 0; draw < sNoiseMeanDraws; ++draw)
                      {
                          Stop again = drawn;
                          again.mName = place.mName + std::string(suffix);
                          again.mActions.mCapture.clear();
                          again.mActions.mMean = Actions::Mean{
                              .mFile = folder / (place.mName + std::string(suffix) + ".png"),
                              .mOf = sNoiseMeanDraws,
                          };
                          into.push_back(std::move(again));
                      }
                  };

            // One side of a place: its reference, its bar and the bar's limit where `bar` asks, then its
            // frame and the frame's mean. **The sample offsets are the stop's place in a whole side**,
            // so the other side draws what the first drew, and an A/B compares two reconstructions of
            // one set of draws. A side that asks what the first asked took the frame to the byte at
            // the glow-lit chamber, and the frame's mean to within 50 bytes of 8.3 million, each by
            // one level, which no figure of the report showed: the card's arithmetic under the
            // wavelet (`docs/rtx/architecture.md`, the denoiser), which two runs differ by too.
            constexpr std::size_t stopsASide = 3 + 2 * sNoiseMeanDraws;
            static_assert(stopsASide * std::uint64_t{ sNoiseSampleStride } <= ~std::uint32_t{ 0 },
                "the sample offsets of one place past what a frame number holds");
            const auto drawSide = [&](const Stop& place, const Rtx::ReconstructionRequest& side, const bool bar,
                                      std::vector<Stop>& into) {
                const std::size_t first = into.size();
                if (bar)
                {
                    Stop reference = picture(place, sNoiseReferenceSuffix, sNoiseReferenceFrames, true,
                        referenceOf(side), std::nullopt, Rtx::Upscale::Off);
                    reference.mActions.mDeepCapture = true;
                    into.push_back(std::move(reference));
                    const Stop averaged
                        = picture(place, sNoiseBarSuffix, barFrames, true, side.unfiltered(), held, Rtx::Upscale::Off);
                    into.push_back(averaged);
                    drawMean(place, averaged, sNoiseBarLimitSuffix, into);
                }
                const Stop judged = frame(place, side);
                into.push_back(judged);
                drawMean(place, judged, sNoiseMeanSuffix, into);

                const std::size_t skipped = bar ? 0 : 2 + sNoiseMeanDraws;
                assert(into.size() - first + skipped == stopsASide);
                for (std::size_t at = first; at < into.size(); ++at)
                    into[at].mSchedule.mSampleOffset
                        = static_cast<std::uint32_t>((at - first + skipped) * sNoiseSampleStride);
            };

            std::vector<Stop> stops;
            stops.reserve(places.size() * stopsASide * (versus.has_value() ? 2 : 1));
            std::vector<NoiseSide> sides;
            sides.reserve(places.size());
            std::vector<NoiseSide> versusSides;
            versusSides.reserve(versus.has_value() ? places.size() : 0);
            for (const Stop& place : places)
            {
                sides.push_back(NoiseSide{ .mPlace = place.mName, .mFrame = place.mName, .mBar = place.mName });
                drawSide(place, played, true, stops);
                if (!versus.has_value())
                    continue;

                // After the first side's reference, whose exposure every picture after it holds.
                Stop other = place;
                other.mName += sNoiseVersusSuffix;
                versusSides.push_back(NoiseSide{
                    .mPlace = place.mName, .mFrame = other.mName, .mBar = ownBar ? other.mName : place.mName });
                drawSide(other, *versus, ownBar, stops);
            }

            SessionRequest request = sessionFor(command, framed, std::move(stops));
            request.mSuite = run.mSuite;

            if (const int status
                = runHosted(variables, command.mConfig, command.mResources, framed.mWindow, std::move(request));
                status != 0)
                return status;

            const int judged = judgeNoise(folder, sides, barFrames);
            if (!versus.has_value())
                return judged;

            out() << std::format("versus --{}{}\n", asked, ownBar ? ", against a bar of its own" : "");
            return std::max(judged, judgeNoise(folder, versusSides, barFrames));
        }

        /// A film of the keys a window wrote: every take drawn headless, its frames numbered through
        /// the film, then encoded.
        ///
        /// **The plan first, whatever follows.** A film is an hour of rendering, and the lengths the
        /// keys came to are where a wrong one shows: a flight of forty seconds where ten were meant
        /// is a key too far away, and the plan says so before a frame is drawn.
        ///
        /// **Stepped at the film's own rate, settled, unvalidated and natively reconstructed.** The world moves a
        /// frame's worth between frames, so the water and the people move at their own speed in the
        /// video; every walk waits for the cells it collects, so no cell arrives on screen; the
        /// layers, which a film does not ask about, stay off unless named, as a bench's do; and every
        /// pixel is traced (`sFilmUpscale`).
        int commandFilm(const Command& command)
        {
            const bpo::variables_map& variables = command.mVariables;
            Framed framed = frameFrom(command);

            // Watched and never summed, like a bench.
            framed.mSetup.mRun.mProfile.mRadianceWidth = Rtx::RadianceWidth::Shown;

            const std::filesystem::path keys = variables["keys"].as<std::string>();
            if (keys.empty())
                throw std::runtime_error("a film needs --keys=<file>, the keys `view --keys` appends on Home");

            // **The step is the run's, and the film counts every length in it**: the world moves a
            // frame of film between two frames.
            framed.mStep = 1.0f / variables["fps"].as<float>();
            framed.mSetup.mSettled = true;
            framed.mSetup.mRun.mProfile.mUpscale = sFilmUpscale;

            FilmPacing pacing;
            pacing.mStep = *framed.mStep;
            pacing.mSpeed = variables["speed"].as<float>();
            pacing.mEase = variables["ease"].as<float>();
            pacing.mLength = filmLengthFrom(variables);
            pacing.mPanSeconds = variables["pan-seconds"].as<float>();
            pacing.mHourSeconds = variables["hour-seconds"].as<float>();
            pacing.mCrossingSeconds = variables["crossing"].as<float>();
            pacing.mStillSeconds = variables["still"].as<float>();
            pacing.mCutDistance = variables["cut-distance"].as<float>();
            pacing.mFieldOfView = framed.mWindow.mFieldOfView;
            pacing.mAspect = static_cast<float>(framed.mWindow.mWidth) / static_cast<float>(framed.mWindow.mHeight);
            pacing.mDay = framed.mDay;
            pacing.mWeatherHold = variables["weather-hold"].as<float>();
            if (variables.count("clock") > 0)
                pacing.mClock = variables["clock"].as<float>();
            pacing.mTurn = weathersToTurn(variables);

            const FilmPlan plan = planFilm(loadKeys(keys), pacing);
            out() << describePlan(plan) << std::flush;
            if (variables["plan"].as<bool>())
                return 0;

            const std::filesystem::path directory = outOf(command);
            const std::filesystem::path frames = directory / "frames";
            std::filesystem::create_directories(frames);
            if (const std::size_t cleared = clearFrames(frames); cleared > 0)
                out() << std::format("cleared {} frames of the last film\n", cleared);

            if (const int status = runHosted(variables, command.mConfig, command.mResources, framed.mWindow,
                    sessionFor(command, framed, stopsFor(plan, frames)));
                status != 0)
                return status;

            std::filesystem::path video = directory / keys.stem();
            video += ".mp4";
            const std::string encode = encodeCommand(frames, video, pacing.getRate());
            if (!variables["encode"].as<bool>())
            {
                out() << "\nthe frames are in " << Files::pathToUnicodeString(frames) << "; to encode them:\n"
                      << encode << '\n';
                return 0;
            }

            out() << "\nencoding: " << encode << '\n' << std::flush;
            if (!Platform::Process::runShell(encode).succeeded())
            {
                out() << "the encoder failed; the frames are in " << Files::pathToUnicodeString(frames) << '\n';
                return 1;
            }

            out() << "the film is " << Files::pathToUnicodeString(video) << '\n';
            return 0;
        }

        /// One verb: which command it is, the line `--help` prints for it, and what it does.
        ///
        /// **Which one it is and not what it is called**, because `verbs.hpp` holds the names: an
        /// option says which commands read it in the same terms this table names them in, so the
        /// two cannot drift into a command whose options nothing reaches.
        struct Verb
        {
            Verbs mVerb;
            std::string_view mSummary;
            int (*mRun)(const Command&);
        };

        /// Every command there is.
        ///
        /// **One list and not two.** The usage printed a name and a summary for each and the
        /// dispatch matched each name against a block of its own, so a verb added to one of them and
        /// forgotten in the other was either a command nobody could find or a line of help nothing
        /// answered. In the order `--help` prints them, which is the order they were written to be
        /// read in rather than a sorted one.
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
            out() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                     "Usage: openmw-rtxtool <command> [options]\n\n"
                     "Commands:\n";

            for (const Verb& verb : sVerbs)
                out() << std::format("  {:<8} {}\n", verbName(verb.mVerb), verb.mSummary);

            out() << "\nWith no arguments at all: a window on the ship at Seyda Neen, where the game starts.\n"
                     "The player's configuration is read and never written: what the engine saves on its way\n"
                     "out -- its settings, its log, its key bindings, its Lua storage -- goes to a directory of\n"
                     "this tool's own under the cache path.\n\n"
                  << options;
        }

        int dispatch(int argc, char* argv[])
        {
            Platform::init();

            // The verb is taken straight off the command line rather than declared as a positional.
            // `ConfigurationManager::readConfiguration` walks the variables map and looks every key
            // up in the options description it was handed, so a key that is deliberately not in that
            // description — which is what a hidden positional is — makes it throw.
            //
            // A window is what this is for, so that is what it does when nobody says otherwise —
            // with no arguments at all, or with only options and no verb.
            const bool hasVerb = argc >= 2 && argv[1][0] != '-';
            const std::string_view command = hasVerb ? argv[1] : "view";

            const ToolOptions options
                = makeOptions(Rtx::sValidationByDefault ? Rtx::ValidationLevel::Sync : Rtx::ValidationLevel::Off);

            // Boost skips the first token as the program name; when there is a verb, that token is
            // the verb.
            //
            // **Held, because the line itself says what was asked for and the map does not.** A
            // variables map cannot tell an option somebody wrote from one `openmw.cfg` set or one
            // that came back defaulted, and what a command has to refuse is the first of the three.
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

            // **Before the chain is walked, because this is the directory the engine writes into.**
            // `ownConfigDirectory` says why a hosted run has one of its own; the log below lands
            // there too.
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

            const std::filesystem::path resources = variables["resources"].as<Files::MaybeQuotedPath>();

            // Before the verb, as `--help` is: a switch that answers instead of the command is one
            // the command never sees.
            if (variables["list-views"].as<bool>())
                return runListViews();

            const Verbs chosen = verbNamed(command);
            const auto found = std::find_if(
                sVerbs.begin(), sVerbs.end(), [chosen](const Verb& verb) { return verb.mVerb == chosen; });

            if (found == sVerbs.end())
            {
                out() << "Unknown command: " << command << "\n\n";
                printUsage(options.mDescription);
                return 1;
            }

            // **Before the command runs, because the alternative is a picture of somewhere else.**
            // Every option is declared on one description, so a command took every one of them and
            // read the ones it knew about: `shot --views=balmora` rendered the default view at
            // Seyda Neen and reported it without a word.
            if (const std::string complaint = options.complainAbout(line, chosen); !complaint.empty())
            {
                out() << complaint;
                return 1;
            }

            // **Before any verb makes a device, because the driver reads where its cache is once.**
            // A cache of the shaders this run reads and of nothing else (`DriverCache`).
            const Rtx::ShaderSet shaders = shadersFor(resources, found->mVerb, variables["shader-source"].as<bool>());
            const DriverCache driverCache(harnessDirectory(), shaders.mDirectory);
            driverCache.applyToDriver();
            driverCache.sweep();

            return found->mRun(Command{ variables, options, config, resources, shaders, found->mVerb });
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

    // **The catcher, and never a box.** This is a developer harness: it is run from a shell or a
    // task runner, its output is read, and a dialog waiting for a click is a run that never
    // finishes — which for something whose whole point is to be run in a loop is the tool not
    // working. So the catcher's box is off, and the catcher is on: a measured run that crashes or
    // hangs leaves the report a player's game would. Not overwritten, so that a shell can still
    // ask for the box, or turn the catcher off.
    Platform::Process::setEnvironmentDefault("OPENMW_CRASH_DIALOG", "0");

    return Debug::wrapApplication(RtxTool::run, argc, argv, RtxTool::applicationName);
}
