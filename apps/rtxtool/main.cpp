#include <charconv>
#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options.hpp>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/platform.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/upscale.hpp>
#include <components/rtxbridge/fogbuilder.hpp>
#include <components/rtxbridge/lightbuilder.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/texturebuilder.hpp>
#include <components/rtxbridge/waterbuilder.hpp>

#include <components/settings/settings.hpp>
#include <limits>

#include "cellscene.hpp"
#include "contactsheet.hpp"
#include "options.hpp"
#include "placement.hpp"
#include "shot.hpp"
#include "validationchoice.hpp"
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

        /// Whether the validation layers load without anyone asking.
        ///
        /// **On outside a Release build**, because the alternative is what this fork just spent an
        /// afternoon on: a window that stuttered and froze with nothing in the log, whose cause was
        /// a ray query built with its end behind its start — undefined, silent, and named outright
        /// by GPU-assisted validation the first time it was switched on. A rule the layers can check
        /// is one nobody should have to think to check for.
        ///
        /// They are not free. GPU-assisted validation instruments every shader and costs roughly
        /// half the frame rate, and the layers themselves allocate on the frame path, which is why
        /// the test that counts allocations builds its own device without them. `--validation=false`
        /// turns them off for a measurement, and a Release build never had them.
#ifdef OPENMW_RTX_VALIDATION_BY_DEFAULT
        constexpr bool sValidationByDefault = true;
#else
        constexpr bool sValidationByDefault = false;
#endif

        /// Which layers a run wants, from what the command line asked for.
        ///
        /// Shared because `info` and every other command have to agree: a device that reported its
        /// limits under one set of layers and traced under another would be describing something
        /// nobody ran.
        ///
        /// @param windowed whether this run opens a window, which is the one place GPU-assisted
        ///        validation cannot be left on.
        Rtx::ValidationOptions validationFrom(const bpo::variables_map& variables, bool windowed)
        {
            // Whether a switch was set matters as much as what it says, so both come across.
            const auto asSwitch = [&](const char* name) {
                return CommandSwitch{ variables[name].as<bool>(), !variables[name].defaulted() };
            };

            return RtxTool::chooseValidation(
                asSwitch("validation"), asSwitch("sync-validation"), asSwitch("gpu-validation"), windowed);
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

        Rtx::Upscale parseUpscale(std::string_view text)
        {
            const std::optional<Rtx::Upscale> named = Rtx::upscaleNamed(text);
            if (!named.has_value())
                throw std::runtime_error("not an upscale mode: " + std::string(text));

            return *named;
        }

        /// What `--exposure` asked for: a number to hold it at, or nothing to measure it.
        std::optional<float> parseExposure(std::string_view text)
        {
            if (text == "auto")
                return std::nullopt;

            float value = 0.0f;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc() || end != text.data() + text.size() || !(value > 0.0f))
                throw std::runtime_error("not an exposure: " + std::string(text));

            return value;
        }

        void printUsage(const bpo::options_description& options)
        {
            out() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                     "Usage: openmw-rtxtool <command> [options]\n\n"
                     "Commands:\n"
                     "  info     report the device this renderer would run on\n"
                     "  scene    read a cell and report what the renderer would be handed\n"
                     "  shot     render a cell and write a PNG, with no window\n"
                     "  view     open a window on a cell and fly around it\n"
                     "  textures every texture a cell uses, vanilla beside de-lit, as one sheet\n\n"
                     "With no arguments at all: a window on the ship at Seyda Neen, where the game starts.\n\n"
                  << options;
        }

        int runInfo(const std::filesystem::path& shaderDirectory, const Rtx::ValidationOptions& validation)
        {
            // A one-pixel target: this reports on a device rather than drawing with it, and the
            // default would spend fifty megabytes of images to print a page of text.
            //
            // **The shaders are still named, because standing a renderer up compiles one.**
            // Reporting on a device is not a reason to build half a renderer, and a build whose
            // shaders are missing should say so here rather than at the first frame asked for.
            std::string reason;
            const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createRenderer(
                Rtx::RendererOptions{
                    .mShaderDirectory = shaderDirectory, .mWidth = 1, .mHeight = 1, .mValidation = validation },
                reason);
            if (renderer == nullptr)
            {
                out() << reason << '\n';
                return 1;
            }

            out() << renderer->describeDevice();
            return 0;
        }

        void printCellHeading(const ESM::Cell& cell)
        {
            out() << "cell:        " << (cell.isExterior() ? "exterior " : "interior ") << '"' << cell.mName << '"';
            if (cell.isExterior())
                out() << " at " << cell.getGridX() << ',' << cell.getGridY();
            out() << "\nwater:       " << (cell.hasWater() ? "yes, at z = " + std::to_string(cell.mWater) : "no")
                  << '\n';

            // Only interiors carry an `AMBI`; an exterior's air comes off the weather and the clock,
            // which the cell says nothing about.
            if (!cell.isExterior())
                out() << "fog:         depth " << cell.mAmbi.mFogDensity << ", extinction "
                      << RtxBridge::interiorFog(cell).mExtinction << " per unit\n";
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

        /// Every texture a cell uses, vanilla beside de-lit, and the names to read it by.
        int runTextures(World& world, const std::string& cellSpec, const std::filesystem::path& output, float strength)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            std::set<std::string> loaded;
            readRegion(world, *cell, 0, extractor, loaded);

            const RtxBridge::SceneTextures described(scene, world.getImageManager());
            const ContactSheet sheet = writeContactSheet(described.getDescriptions(), output, strength);
            if (sheet.mCount == 0)
            {
                out() << "The cell uses no textures.\n";
                return 1;
            }

            // The sheet carries no lettering, so the order is printed instead: left to right, top to
            // bottom, the way it was drawn.
            const std::span<const VFS::Path::Normalized> paths = scene.getTextures();
            for (std::size_t i = 0; i < paths.size(); ++i)
                out() << "  " << i << "  " << paths[i] << '\n';

            out() << "wrote " << Files::pathToUnicodeString(output) << ", " << sheet.mCount
                  << " textures at --delight=" << strength << '\n';
            return 0;
        }

        int runScene(World& world, const std::string& cellSpec, int radius, bool twice)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            std::set<std::string> loaded;
            const CellReport report = readRegion(world, *cell, radius, extractor, loaded);

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
                  << ", " << report.mAmbient.y() << ", " << report.mAmbient.z() << '\n'
                  << "  deforming drawables:  " << report.mStats.mDeformed << '\n';

            out() << "\nnot placed\n"
                  << "  record type unread:   " << report.mSkipped.mUnknownType << '\n'
                  << "  record has no model:  " << report.mSkipped.mNoModel << '\n'
                  << "  model would not load: " << report.mUnreadable << '\n'
                  << "  unreadable drawables: " << report.mStats.mSkippedUnknown << '\n'
                  << "  empty geometry:       " << report.mStats.mSkippedEmpty << '\n';

            if (twice)
            {
                // Terrain and objects together: the property is about the whole graph, and terrain
                // is half the geometry in an exterior.
                // The same cells again, which the loaded set would refuse — the property being
                // measured is what a second walk over an unchanged graph adds, so it is asked with
                // a set that has never heard of them.
                std::set<std::string> again;
                const CellReport second = readRegion(world, *cell, radius, extractor, again);
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
        int runShot(World& world, const std::string& cellSpec, int radius, const Rtx::ValidationOptions& validation,
            ShotRequest request)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            std::set<std::string> loaded;
            request.mLighting
                = loadRegion(world, *cell, radius, scene, extractor, loaded, request.mWeather, request.mHour);

            printCellHeading(*cell);
            out() << '\n';

            return renderShot(scene, world.getImageManager(), validation, request);
        }

        int runView(World& world, const std::string& cellSpec, int radius, const Rtx::ValidationOptions& validation,
            ViewRequest request)
        {
            const ESM::Cell* cell = findCellOrComplain(world, cellSpec);
            if (cell == nullptr)
                return 1;

            printCellHeading(*cell);

            return runWindow(world, *cell, radius, validation, std::move(request));
        }

        /// Where the command line and the view file meet.
        ///
        /// A named view supplies the cell and usually the camera; anything given explicitly on the
        /// command line wins over it, so a view is a starting point rather than a straitjacket.
        struct Chosen
        {
            std::string mCell;
            std::string mTitle;

            /// The view file's id and note, empty where nothing named one.
            std::string mView;
            std::string mNote;

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
            chosen.mView = view->mName;
            chosen.mNote = view->mNote;
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

            bpo::options_description options = makeOptionsDescription(sValidationByDefault);

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

            const std::filesystem::path resources = variables["resources"].as<Files::MaybeQuotedPath>();

            if (command == "info")
            {
                const Rtx::ValidationOptions validation = validationFrom(variables, false);

                return runInfo(resources / "rtx" / "shaders", validation);
            }

            if (variables["list-views"].as<bool>())
                return runListViews(resources);

            if (command == "textures")
            {
                const Chosen chosen = chooseView(variables, resources);
                World world(config, variables, resources);

                return runTextures(
                    world, chosen.mCell, variables["out"].as<std::string>(), variables["delight"].as<float>());
            }

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

                return runScene(world, chosen.mCell, static_cast<int>(variables["radius"].as<std::uint32_t>()),
                    variables["twice"].as<bool>());
            }

            if (command == "shot" || command == "view")
            {
                const auto [width, height] = parseSize(variables["size"].as<std::string>());

                // With nothing on the command line, the ship at Seyda Neen: where the game starts,
                // and the one place every player of this game has stood.
                const Chosen chosen = chooseView(variables, resources);

                const Rtx::ValidationOptions validation = validationFrom(variables, command == "view");

                World world(config, variables, resources);

                if (command == "view")
                {
                    ViewRequest request;
                    request.mTitle = chosen.mTitle;
                    request.mView = chosen.mView;
                    request.mNote = chosen.mNote;
                    request.mCell = chosen.mCell;
                    request.mShaderDirectory = resources / "rtx" / "shaders";
                    request.mScreenshotDirectory = config.getScreenshotPath();
                    request.mWidth = width;
                    request.mHeight = height;
                    request.mFieldOfView = variables["fov"].as<float>();
                    request.mOrigin = chosen.mOrigin;
                    request.mTarget = chosen.mTarget;
                    request.mFrames = variables["frames"].as<std::uint32_t>();
                    request.mShowAlbedo = variables["albedo"].as<bool>();
                    request.mFilter = variables["filter"].as<bool>();
                    request.mExposure = parseExposure(variables["exposure"].as<std::string>());
                    request.mUpscale = parseUpscale(variables["upscale"].as<std::string>());
                    request.mDelight = variables["delight"].as<float>();
                    request.mWeather = variables["weather"].as<std::string>();
                    request.mHour = variables["hour"].as<float>();

                    return runView(world, chosen.mCell, static_cast<int>(variables["radius"].as<std::uint32_t>()),
                        validation, request);
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
                request.mJitter = variables["jitter"].as<bool>();
                request.mFilter = variables["filter"].as<bool>();
                request.mExposure = parseExposure(variables["exposure"].as<std::string>());
                request.mDelight = variables["delight"].as<float>();
                request.mWeather = variables["weather"].as<std::string>();
                request.mHour = variables["hour"].as<float>();
                request.mUpscale = parseUpscale(variables["upscale"].as<std::string>());

                // **A reference cannot be built through a denoiser.** `--accumulate` averages frames
                // towards the truth and Ray Reconstruction resolves each of them towards its own
                // opinion, so a thousand of those converge on the network rather than on the
                // integral — the same argument `mFilter` carries, one denoiser along. Turned off
                // rather than refused, because the default is on and nobody asking for a reference
                // is asking for this; someone who names `--upscale` too gets what they named.
                if (variables["accumulate"].as<std::uint32_t>() > 0 && variables["upscale"].defaulted())
                    request.mUpscale = Rtx::Upscale::Off;
                request.mRepeat = variables["repeat"].as<std::uint32_t>();
                request.mAccumulate = variables["accumulate"].as<std::uint32_t>();

                return runShot(world, chosen.mCell, static_cast<int>(variables["radius"].as<std::uint32_t>()),
                    validation, request);
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
