#include <ostream>
#include <string>
#include <string_view>

#include <boost/program_options.hpp>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/platform/platform.hpp>
#include <components/rtx/device.hpp>
#include <components/rtx/instance.hpp>
#include <components/rtx/physicaldevice.hpp>
#include <components/rtx/requirements.hpp>

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

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

        void printUsage(const bpo::options_description& options)
        {
            out() << "Drives the experimental ray tracing renderer without the game window.\n\n"
                     "Usage: openmw-rtxtool <command> [options]\n\n"
                     "Commands:\n"
                     "  info    report the device this renderer would run on\n\n"
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

        int run(int argc, char* argv[])
        {
            Platform::init();

            bpo::options_description options("Options");
            auto addOption = options.add_options();
            addOption("help", "print this message and quit");
            addOption("validation", bpo::bool_switch(), "load VK_LAYER_KHRONOS_validation");
            addOption("sync-validation", bpo::bool_switch(),
                "add synchronization validation, which catches missing barriers (implies --validation)");
            addOption("gpu-validation", bpo::bool_switch(),
                "add GPU-assisted validation, which instruments shaders (implies --validation)");

            bpo::options_description hidden;
            hidden.add_options()("command", bpo::value<std::string>(), "");

            bpo::options_description all;
            all.add(options).add(hidden);

            bpo::positional_options_description positional;
            positional.add("command", 1);

            bpo::variables_map variables;
            bpo::store(bpo::command_line_parser(argc, argv).options(all).positional(positional).run(), variables);
            bpo::notify(variables);

            if (variables.count("help") > 0 || variables.count("command") == 0)
            {
                printUsage(options);
                return variables.count("help") > 0 ? 0 : 1;
            }

            Rtx::InstanceOptions instanceOptions;
            instanceOptions.mSynchronizationValidation = variables["sync-validation"].as<bool>();
            instanceOptions.mGpuAssistedValidation = variables["gpu-validation"].as<bool>();
            instanceOptions.mValidation = variables["validation"].as<bool>()
                || instanceOptions.mSynchronizationValidation || instanceOptions.mGpuAssistedValidation;

            const std::string command = variables["command"].as<std::string>();
            if (command == "info")
                return runInfo(instanceOptions);

            out() << "Unknown command: " << command << "\n\n";
            printUsage(options);
            return 1;
        }
    }
}

int main(int argc, char* argv[])
{
    return Debug::wrapApplication(RtxTool::run, argc, argv, RtxTool::applicationName);
}
