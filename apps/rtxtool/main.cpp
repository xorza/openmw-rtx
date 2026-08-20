#include <cstdint>
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
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/settings/settings.hpp>

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

            addOption("cell", bpo::value<std::string>()->default_value("Seyda Neen, Census and Excise Office"),
                "cell to read, addressed the way Morrowind does: a pair of integers is an exterior, "
                "anything else is an interior's name. Write --cell=-2,-9 rather than --cell -2,-9, or "
                "the leading minus reads as an option.");

            addOption("twice", bpo::bool_switch(),
                "extract the cell a second time and report what the second pass added, which should "
                "be nothing");

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

        void printUsage(const bpo::options_description& options)
        {
            out() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                     "Usage: openmw-rtxtool <command> [options]\n\n"
                     "Commands:\n"
                     "  info     report the device this renderer would run on\n"
                     "  scene    read a cell and report what the renderer would be handed\n\n"
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

        int runScene(World& world, const std::string& cellSpec, bool twice)
        {
            const ESM::Cell* cell = world.findCell(cellSpec);
            if (cell == nullptr)
            {
                out() << "No cell is called \"" << cellSpec << "\".\n";
                return 1;
            }

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);

            const auto readCell = [&](RtxBridge::ExtractionStats& totals, std::uint32_t& unreadable) {
                return world.forEachObject(*cell, [&](const World::Object& object) {
                    osg::ref_ptr<const osg::Node> node;
                    try
                    {
                        node = world.getSceneManager().getTemplate(object.mModel, false);
                    }
                    catch (const std::exception& e)
                    {
                        Log(Debug::Warning) << "Cannot load " << object.mModel << ": " << e.what();
                        ++unreadable;
                        return;
                    }

                    totals += extractor.extract(*node, object.mTransform);
                });
            };

            RtxBridge::ExtractionStats stats;
            std::uint32_t unreadable = 0;
            const World::SkippedObjects skipped = readCell(stats, unreadable);

            out() << "cell:        " << (cell->isExterior() ? "exterior " : "interior ") << '"' << cell->mName << '"';
            if (cell->isExterior())
                out() << " at " << cell->getGridX() << ',' << cell->getGridY();
            out() << "\nwater:       " << (cell->hasWater() ? "yes, at z = " + std::to_string(cell->mWater) : "no")
                  << '\n';

            out() << "\nplaced\n"
                  << "  instances:            " << stats.mInstances << '\n'
                  << "  meshes:               " << scene.getMeshes().size() << '\n'
                  << "  materials:            " << scene.getMaterials().size() << '\n'
                  << "  textures:             " << scene.getTextures().size() << '\n'
                  << "  triangles:            " << scene.getTriangleCount() << '\n'
                  << "  vertex+index bytes:   " << scene.getGeometryBytes() / 1024 << " KiB\n";

            out() << "\nnot placed\n"
                  << "  record type unread:   " << skipped.mUnknownType << '\n'
                  << "  record has no model:  " << skipped.mNoModel << '\n'
                  << "  model would not load: " << unreadable << '\n'
                  << "  deformed drawables:   " << stats.mSkippedDeformed << '\n'
                  << "  empty geometry:       " << stats.mSkippedEmpty << '\n';

            if (twice)
            {
                RtxBridge::ExtractionStats second;
                std::uint32_t ignored = 0;
                readCell(second, ignored);

                out() << "\nsecond pass over the same graph\n"
                      << "  new meshes:           " << second.mMeshesAdded << " (should be 0)\n"
                      << "  new materials:        " << second.mMaterialsAdded << " (should be 0)\n"
                      << "  drawables resolved:   " << second.mMeshesReused << " to a known mesh\n";
            }

            return 0;
        }

        int dispatch(int argc, char* argv[])
        {
            Platform::init();

            // The verb is taken straight off the command line rather than declared as a positional.
            // `ConfigurationManager::readConfiguration` walks the variables map and looks every key
            // up in the options description it was handed, so a key that is deliberately not in that
            // description — which is what a hidden positional is — makes it throw.
            if (argc < 2 || argv[1][0] == '-')
            {
                printUsage(makeOptionsDescription());
                return argc >= 2 && std::string_view(argv[1]) == "--help" ? 0 : 1;
            }

            const std::string command = argv[1];

            bpo::options_description options = makeOptionsDescription();

            // Boost skips the first token as the program name; here that token is the verb.
            bpo::variables_map variables;
            bpo::store(bpo::command_line_parser(argc - 1, argv + 1).options(options).run(), variables);
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

            if (command == "scene")
            {
                World world(config, variables, variables["resources"].as<Files::MaybeQuotedPath>());
                return runScene(world, variables["cell"].as<std::string>(), variables["twice"].as<bool>());
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
