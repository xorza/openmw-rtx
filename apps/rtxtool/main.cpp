#include <charconv>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options.hpp>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/platform/platform.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/device.hpp>
#include <components/rtx/instance.hpp>
#include <components/rtx/physicaldevice.hpp>
#include <components/rtx/requirements.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/lightbuilder.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/waterbuilder.hpp>

#include <components/settings/settings.hpp>
#include <limits>

#include "placement.hpp"
#include "shot.hpp"
#include "view.hpp"
#include "views.hpp"
#include "world.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        using StringsVector = std::vector<std::string>;

        constexpr std::string_view applicationName = "RtxTool";

        /// Reports go to the unprefixed stream.
        ///
        /// `Debug::wrapApplication` routes `std::cout` through the log formatter, which stamps every
        /// line with a time and a level. That is right for a game and wrong for a tool whose output
        /// is meant to be read, diffed, or piped into something that parses it.
        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        bpo::options_description makeOptionsDescription()
        {
            bpo::options_description result("Options");
            auto addOption = result.add_options();
            addOption("help", "print this message and quit");

            addOption("validation", bpo::bool_switch(), "load VK_LAYER_KHRONOS_validation");
            addOption("sync-validation", bpo::bool_switch(),
                "add synchronization validation, which catches missing barriers (implies --validation)");
            addOption("gpu-validation", bpo::bool_switch(),
                "add GPU-assisted validation, which instruments shaders (implies --validation)");

            addOption("cell", bpo::value<std::string>()->default_value(""),
                "cell to read, addressed the way Morrowind does: a pair of integers is an exterior, "
                "anything else is an interior's name. Write --cell=-2,-9 rather than --cell -2,-9, or "
                "the leading minus reads as an option. Left out, the default view decides.");

            addOption("twice", bpo::bool_switch(),
                "extract the cell a second time and report what the second pass added, which should "
                "be nothing");

            addOption("view", bpo::value<std::string>()->default_value(""),
                "a named viewpoint from resources/rtx/views.cfg, which supplies the cell and usually the "
                "camera. Overrides --cell.");

            addOption("list-views", bpo::bool_switch(), "print the named viewpoints and quit");

            addOption("albedo", bpo::bool_switch(),
                "write the albedo with no shading over it, which is what a texture problem looks like "
                "when nothing else is in the way");

            addOption("weather", bpo::value<std::string>()->default_value("Clear"),
                "which weather's sun and sky an exterior stands under, named as the content files "
                "spell it: Clear, Cloudy, Foggy, Overcast, Rain, Thunderstorm, Ashstorm, Blight");

            addOption("hour", bpo::value<float>()->default_value(12.0f),
                "what time an exterior's sun is at, on a twenty-four hour clock. An interior is lit "
                "by its own lamps and does not care");

            addOption("frames", bpo::value<std::uint32_t>()->default_value(0),
                "with `view`, close after this many frames instead of waiting to be closed");

            addOption("find", bpo::value<std::string>()->default_value(""),
                "with `scene`, print the world position of every object whose model path contains this. "
                "How the coordinates in a view are found.");

            addOption("out", bpo::value<std::string>()->default_value("shot.png"), "where to write the image");
            addOption("size", bpo::value<std::string>()->default_value("1920x1080"), "image size, as WIDTHxHEIGHT");
            addOption("fov", bpo::value<float>()->default_value(60.0f), "vertical field of view, in degrees");
            addOption("pos", bpo::value<std::string>()->default_value(""),
                "where to put the camera, as x,y,z. Defaults to a view of the whole cell from outside it, "
                "which is a poor view of an interior. Write --pos=-100,200,300, or a leading minus reads "
                "as an option.");
            addOption("look", bpo::value<std::string>()->default_value(""),
                "what the camera looks at, as x,y,z. Defaults to the centre of the cell.");

            addOption("data",
                bpo::value<Files::MaybeQuotedPathContainer>()
                    ->default_value(Files::MaybeQuotedPathContainer(), "data")
                    ->multitoken()
                    ->composing(),
                "set data directories (later directories have higher priority)");

            addOption("data-local",
                bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
                    Files::MaybeQuotedPathContainer::value_type(), ""),
                "set local data directory (highest priority)");

            addOption("fallback-archive",
                bpo::value<StringsVector>()
                    ->default_value(StringsVector(), "fallback-archive")
                    ->multitoken()
                    ->composing(),
                "set fallback BSA archives (later archives have higher priority)");

            addOption("content",
                bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
                "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");

            addOption("encoding", bpo::value<std::string>()->default_value("win1252"),
                "character encoding of the content files");

            addOption("fallback",
                bpo::value<Fallback::FallbackMap>()
                    ->default_value(Fallback::FallbackMap(), "")
                    ->multitoken()
                    ->composing(),
                "fallback values");

            Files::ConfigurationManager::addCommonOptions(result);

            return result;
        }

        /// Parses `WIDTHxHEIGHT`.
        std::pair<std::uint32_t, std::uint32_t> parseSize(std::string_view text)
        {
            const std::size_t cross = text.find('x');
            std::uint32_t width = 0;
            std::uint32_t height = 0;

            const bool ok = cross != std::string_view::npos
                && std::from_chars(text.data(), text.data() + cross, width).ec == std::errc()
                && std::from_chars(text.data() + cross + 1, text.data() + text.size(), height).ec == std::errc();

            if (!ok || width == 0 || height == 0)
                throw std::runtime_error("not a size: " + std::string(text));

            return { width, height };
        }

        void printUsage(const bpo::options_description& options)
        {
            out() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                     "Usage: openmw-rtxtool <command> [options]\n\n"
                     "Commands:\n"
                     "  info     report the device this renderer would run on\n"
                     "  scene    read a cell and report what the renderer would be handed\n"
                     "  shot     render a cell and write a PNG, with no window\n"
                     "  view     open a window on a cell and fly around it\n\n"
                     "With no arguments at all: a window on the ship at Seyda Neen, where the game starts.\n\n"
                  << options;
        }

        int runInfo(const Rtx::InstanceOptions& instanceOptions)
        {
            const Rtx::Instance instance(instanceOptions);

            out() << "loader:            Vulkan " << Rtx::versionString(instance.getApiVersion()) << '\n'
                  << "validation:        " << (instance.getValidationLog() != nullptr ? "on" : "off") << '\n'
                  << "debug utils:       " << (instance.hasDebugUtils() ? "on" : "off") << '\n';

            Rtx::PhysicalDevice physicalDevice = Rtx::PhysicalDevice::select(instance.getHandle());
            out() << physicalDevice.describe();

            // Creating the device is the part that proves the report: it resolves every entry point
            // the required extensions promise, and a driver that advertises one it cannot dispatch
            // fails here rather than at the first frame that needed it.
            const Rtx::Device device(instance, std::move(physicalDevice));
            out() << "\nlogical device and every required entry point: ok\n";

            return 0;
        }

        /// What reading a cell produced besides the scene itself.
        struct CellReport
        {
            RtxBridge::ExtractionStats mStats;
            RtxBridge::ExtractionStats mTerrain;
            World::SkippedObjects mSkipped;

            /// References whose model is named but will not load. Logged individually as they fail.
            std::uint32_t mUnreadable = 0;

            /// What the cell's `LIGH` references cast. Carried, negative and off-by-default records
            /// place a mesh and no light, so this is shorter than the cell's list of them.
            ///
            /// Reported rather than placed, because reading a cell twice must not light it twice.
            /// Geometry is safe from that — the extractor recognises what it has already seen — and
            /// a light has no such identity, so the decision to place one belongs to whoever knows
            /// whether this cell is already in the scene.
            std::vector<Rtx::Light> mLights;

            /// The cell's ambient, linear. Black for an exterior, whose sky is M5's.
            osg::Vec3f mAmbient;
        };

        /// Mirrors one cell's geometry through `extractor`, and reports what else it holds.
        ///
        /// The content arrives by two routes and only one of them is the scene graph: lights are not
        /// in it at all, because `NifOsg` never reads `NiLight`. They come off the `LIGH` records the
        /// same references point at, and leave here in the report.
        CellReport readCell(World& world, const ESM::Cell& cell, RtxBridge::SceneExtractor& extractor)
        {
            CellReport report;

            // Terrain first, because it is what everything else stands on and its absence is the
            // loudest thing about a screenshot that lacks it.
            if (const osg::ref_ptr<osg::Node> terrain = world.buildTerrain(cell))
                report.mTerrain = extractor.extract(*terrain, osg::Matrixf::identity());

            // Interiors were authored against a renderer with no bounce, so the cell's own ambient
            // is most of what lights one. An exterior's `AMBI` is the weather system's business and
            // is not read here.
            if (!cell.isExterior() && cell.mHasAmbi)
                report.mAmbient = RtxBridge::decodeColour(cell.mAmbi.mAmbient);

            report.mSkipped = world.forEachObject(cell, [&](const World::Object& object) {
                if (object.mLight != nullptr)
                    if (const std::optional<Rtx::Light> light
                        = RtxBridge::makeLight(*object.mLight, object.mTransform.getTrans()))
                        report.mLights.push_back(*light);

                osg::ref_ptr<const osg::Node> node;
                try
                {
                    node = world.getSceneManager().getTemplate(object.mModel, false);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Cannot load " << object.mModel << ": " << e.what();
                    ++report.mUnreadable;
                    return;
                }

                report.mStats += extractor.extract(*node, object.mTransform);
            });

            return report;
        }

        void printCellHeading(const ESM::Cell& cell)
        {
            out() << "cell:        " << (cell.isExterior() ? "exterior " : "interior ") << '"' << cell.mName << '"';
            if (cell.isExterior())
                out() << " at " << cell.getGridX() << ',' << cell.getGridY();
            out() << "\nwater:       " << (cell.hasWater() ? "yes, at z = " + std::to_string(cell.mWater) : "no")
                  << '\n';
        }

        /// The named cell, or null with the complaint already printed.
        const ESM::Cell* findCellOrComplain(World& world, const std::string& cellSpec)
        {
            const ESM::Cell* cell = world.findCell(cellSpec);
            if (cell == nullptr)
                out() << "No cell is called \"" << cellSpec << "\".\n";

            return cell;
        }

        /// Prints where every object whose model path contains `needle` stands.
        ///
        /// A cell is thousands of references and a view wants to point at one of them. Grepping the
        /// content files gives a model name; this gives the place it was put.
        int runFind(World& world, const ESM::Cell& cell, const std::string& needle)
        {
            std::uint32_t found = 0;
            world.forEachObject(cell, [&](const World::Object& object) {
                if (object.mModel.value().find(needle) == std::string::npos)
                    return;

                const osg::Vec3f at = object.mTransform.getTrans();
                out() << "  " << at.x() << ", " << at.y() << ", " << at.z() << "   " << object.mModel << '\n';
                ++found;
            });

            out() << found << " objects match \"" << needle << "\"\n";
            return 0;
        }

        int runScene(World& world, const std::string& cellSpec, bool twice)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            const CellReport report = readCell(world, *cell, extractor);

            printCellHeading(*cell);

            out() << "\nplaced\n"
                  << "  terrain chunks:       " << report.mTerrain.mInstances << '\n'
                  << "  object instances:     " << report.mStats.mInstances << '\n'
                  << "  meshes:               " << scene.getMeshes().size() << '\n'
                  << "  materials:            " << scene.getMaterials().size() << '\n'
                  << "  textures:             " << scene.getTextures().size() << '\n'
                  << "  triangles:            " << scene.getTriangleCount() << '\n'
                  << "  vertex+index bytes:   " << scene.getGeometryBytes() / 1024 << " KiB\n";

            for (const auto& [format, count] : report.mStats.mTextureFormats)
                out() << "  " << count << " x " << format << '\n';

            // Which materials traversal will have to stop and ask about, and which of those asked
            // for it outright. The second number being the small one is the point: Morrowind keeps
            // its foliage under `NiAlphaProperty` rather than under an alpha test.
            std::uint32_t cutouts = 0;
            std::uint32_t tested = 0;
            std::uint32_t glowing = 0;
            for (const Rtx::Material& material : scene.getMaterials())
            {
                cutouts += material.isCutout() ? 1 : 0;
                tested += material.mAlphaMode == Rtx::AlphaMode::Cutout ? 1 : 0;
                glowing += material.mEmissiveColour.length2() > 0.0f || material.mEmissive != Rtx::sNoIndex ? 1 : 0;
            }
            out() << "  cutout materials:     " << cutouts << ", " << tested << " of them alpha-tested outright\n"
                  << "  emissive materials:   " << glowing << '\n'
                  << "  lights:               " << report.mLights.size() << " casting, ambient " << report.mAmbient.x()
                  << ", " << report.mAmbient.y() << ", " << report.mAmbient.z() << '\n';

            out() << "\nnot placed\n"
                  << "  record type unread:   " << report.mSkipped.mUnknownType << '\n'
                  << "  record has no model:  " << report.mSkipped.mNoModel << '\n'
                  << "  model would not load: " << report.mUnreadable << '\n'
                  << "  deformed drawables:   " << report.mStats.mSkippedDeformed << '\n'
                  << "  empty geometry:       " << report.mStats.mSkippedEmpty << '\n';

            if (twice)
            {
                // Terrain and objects together: the property is about the whole graph, and terrain
                // is half the geometry in an exterior.
                const CellReport second = readCell(world, *cell, extractor);
                RtxBridge::ExtractionStats total = second.mStats;
                total += second.mTerrain;

                out() << "\nsecond pass over the same graph\n"
                      << "  new meshes:           " << total.mMeshesAdded << " (should be 0)\n"
                      << "  new materials:        " << total.mMaterialsAdded << " (should be 0)\n"
                      << "  drawables resolved:   " << total.mMeshesReused << " to a known mesh\n";
            }

            return 0;
        }

        /// Reads a cell and places all of it, lights included.
        ///
        /// An interior's illumination is its own lamps over its own `AMBI`; an exterior's is the sky
        /// and the weather, which the cell says nothing about and the clock decides.
        CellLighting loadCell(World& world, const ESM::Cell& cell, Rtx::SceneDesc& scene,
            RtxBridge::SceneExtractor& extractor, std::string_view weather, float hour)
        {
            const CellReport report = readCell(world, cell, extractor);
            for (const Rtx::Light& light : report.mLights)
                scene.addLight(light);

            // After the geometry, because an interior's pool is sized by what the room holds and
            // there is nothing to measure until the room is in. Here rather than in `readCell` for
            // the reason the lights are: reading a cell twice must not fill it twice.
            const std::optional<float> water = RtxBridge::addWater(scene, cell);

            const float level = water.value_or(-std::numeric_limits<float>::infinity());

            // An interior's sky is never seen and its sun never shines, so the daylight stays dark.
            if (!cell.isExterior())
                return CellLighting{ .mAmbient = report.mAmbient, .mWaterLevel = level, .mDaylight = {} };

            const RtxBridge::Daylight daylight = RtxBridge::makeDaylight(weather, hour);
            return CellLighting{ .mAmbient = daylight.mAmbient, .mWaterLevel = level, .mDaylight = daylight };
        }

        int runShot(
            World& world, const std::string& cellSpec, const Rtx::InstanceOptions& instanceOptions, ShotRequest request)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            request.mLighting = loadCell(world, *cell, scene, extractor, request.mWeather, request.mHour);

            printCellHeading(*cell);
            out() << '\n';

            return renderShot(scene, world.getImageManager(), instanceOptions, request);
        }

        int runView(
            World& world, const std::string& cellSpec, const Rtx::InstanceOptions& instanceOptions, ViewRequest request)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            request.mLighting = loadCell(world, *cell, scene, extractor, request.mWeather, request.mHour);

            printCellHeading(*cell);

            return runWindow(scene, world.getImageManager(), instanceOptions, request);
        }

        /// Where the command line and the view file meet.
        ///
        /// A named view supplies the cell and usually the camera; anything given explicitly on the
        /// command line wins over it, so a view is a starting point rather than a straitjacket.
        struct Chosen
        {
            std::string mCell;
            std::string mTitle;
            std::optional<osg::Vec3f> mOrigin;
            std::optional<osg::Vec3f> mTarget;
        };

        /// Where someone starts when they have said nothing about where: the ship at Seyda Neen,
        /// where the game starts and the one place every player of it has stood.
        constexpr std::string_view sDefaultView = "seyda-neen-ship";

        Chosen chooseView(const bpo::variables_map& variables, const std::filesystem::path& resources)
        {
            Chosen chosen{
                .mCell = variables["cell"].as<std::string>(),
                .mTitle = "OpenMW RTX",
                .mOrigin = parseVec3(variables["pos"].as<std::string>(), "--pos"),
                .mTarget = parseVec3(variables["look"].as<std::string>(), "--look"),
            };

            std::string name = variables["view"].as<std::string>();
            if (name.empty())
            {
                if (!chosen.mCell.empty())
                    return chosen;

                name = sDefaultView;
            }

            const std::vector<View> views = loadViews(resources / "rtx" / "views.cfg");
            const View* view = findView(views, name);
            if (view == nullptr)
            {
                std::string known;
                for (const View& candidate : views)
                    known += "\n  " + candidate.mName + "   " + candidate.mNote;

                throw std::runtime_error("no view is called \"" + name + "\". These are:" + known);
            }

            chosen.mCell = view->mCell;
            chosen.mTitle = "OpenMW RTX - " + view->mName;
            if (!chosen.mOrigin)
                chosen.mOrigin = view->mOrigin;
            if (!chosen.mTarget)
                chosen.mTarget = view->mTarget;

            return chosen;
        }

        int runListViews(const std::filesystem::path& resources)
        {
            for (const View& view : loadViews(resources / "rtx" / "views.cfg"))
                out() << "  " << view.mName << "\n      " << view.mCell << "\n      " << view.mNote << '\n';

            return 0;
        }

        int dispatch(int argc, char* argv[])
        {
            Platform::init();

            // The verb is taken straight off the command line rather than declared as a positional.
            // `ConfigurationManager::readConfiguration` walks the variables map and looks every key
            // up in the options description it was handed, so a key that is deliberately not in that
            // description — which is what a hidden positional is — makes it throw.
            // A window is what this is for, so that is what it does when nobody says otherwise —
            // with no arguments at all, or with only options and no verb.
            const bool hasVerb = argc >= 2 && argv[1][0] != '-';
            const std::string command = hasVerb ? argv[1] : "view";

            bpo::options_description options = makeOptionsDescription();

            // Boost skips the first token as the program name; here that token is the verb.
            // Boost skips the first token as the program name; when there is a verb, that token is
            // the verb.
            bpo::variables_map variables;
            bpo::store(hasVerb ? bpo::command_line_parser(argc - 1, argv + 1).options(options).run()
                               : bpo::command_line_parser(argc, argv).options(options).run(),
                variables);
            bpo::notify(variables);

            if (variables.count("help") > 0)
            {
                printUsage(options);
                return 0;
            }

            Files::ConfigurationManager config;
            config.processPaths(variables, std::filesystem::current_path());
            config.readConfiguration(variables, options);
            Debug::setupLogging(config.getLogPath(), applicationName);
            Settings::Manager::load(config);

            if (command == "info")
            {
                Rtx::InstanceOptions instanceOptions;
                instanceOptions.mSynchronizationValidation = variables["sync-validation"].as<bool>();
                instanceOptions.mGpuAssistedValidation = variables["gpu-validation"].as<bool>();
                instanceOptions.mValidation = variables["validation"].as<bool>()
                    || instanceOptions.mSynchronizationValidation || instanceOptions.mGpuAssistedValidation;

                return runInfo(instanceOptions);
            }

            const std::filesystem::path resources = variables["resources"].as<Files::MaybeQuotedPath>();

            if (variables["list-views"].as<bool>())
                return runListViews(resources);

            if (command == "scene")
            {
                const Chosen chosen = chooseView(variables, resources);
                World world(config, variables, resources);

                const std::string needle = variables["find"].as<std::string>();
                if (!needle.empty())
                {
                    const ESM::Cell* cell = findCellOrComplain(world, chosen.mCell);
                    return cell == nullptr ? 1 : runFind(world, *cell, needle);
                }

                return runScene(world, chosen.mCell, variables["twice"].as<bool>());
            }

            if (command == "shot" || command == "view")
            {
                const auto [width, height] = parseSize(variables["size"].as<std::string>());

                // With nothing on the command line, the ship at Seyda Neen: where the game starts,
                // and the one place every player of this game has stood.
                const Chosen chosen = chooseView(variables, resources);

                Rtx::InstanceOptions instanceOptions;
                instanceOptions.mSynchronizationValidation = variables["sync-validation"].as<bool>();
                instanceOptions.mGpuAssistedValidation = variables["gpu-validation"].as<bool>();
                instanceOptions.mValidation = variables["validation"].as<bool>()
                    || instanceOptions.mSynchronizationValidation || instanceOptions.mGpuAssistedValidation;

                World world(config, variables, resources);

                if (command == "view")
                {
                    ViewRequest request;
                    request.mTitle = chosen.mTitle;
                    request.mShaderDirectory = resources / "rtx" / "shaders";
                    request.mScreenshotDirectory = config.getScreenshotPath();
                    request.mWidth = width;
                    request.mHeight = height;
                    request.mFieldOfView = variables["fov"].as<float>();
                    request.mOrigin = chosen.mOrigin;
                    request.mTarget = chosen.mTarget;
                    request.mFrames = variables["frames"].as<std::uint32_t>();
                    request.mShowAlbedo = variables["albedo"].as<bool>();
                    request.mWeather = variables["weather"].as<std::string>();
                    request.mHour = variables["hour"].as<float>();

                    return runView(world, chosen.mCell, instanceOptions, request);
                }

                ShotRequest request;
                request.mOutput = variables["out"].as<std::string>();
                request.mShaderDirectory = resources / "rtx" / "shaders";
                request.mWidth = width;
                request.mHeight = height;
                request.mFieldOfView = variables["fov"].as<float>();
                request.mOrigin = chosen.mOrigin;
                request.mTarget = chosen.mTarget;
                request.mShowAlbedo = variables["albedo"].as<bool>();
                request.mWeather = variables["weather"].as<std::string>();
                request.mHour = variables["hour"].as<float>();

                return runShot(world, chosen.mCell, instanceOptions, request);
            }

            out() << "Unknown command: " << command << "\n\n";
            printUsage(options);
            return 1;
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
    return Debug::wrapApplication(RtxTool::run, argc, argv, RtxTool::applicationName);
}
