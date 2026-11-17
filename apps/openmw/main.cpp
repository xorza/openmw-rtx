#include <boost/program_options/variables_map.hpp>
#include <osg/Notify>

#include <apps/openmw/mwgui/debugwindow.hpp>
#include <components/crashcatcher/crashinstall.hpp>
#include <components/debug/debugging.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/platform/platform.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>

#include "engine.hpp"
#include "options.hpp"

#if defined(_WIN32)
#include <components/misc/windows.hpp>
// makes __argc and __argv available on windows
#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
#endif

#include <chrono>
#include <filesystem>

/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 *
 * Results are directly written to \ref Engine class.
 *
 * \retval true - Everything goes OK
 * \retval false - Error
 */
namespace
{
    bool parseOptions(std::int32_t argc, char** argv, OMW::Engine& engine, Files::ConfigurationManager& cfgMgr)
    {
        // Create a local alias for brevity
        namespace bpo = boost::program_options;
        typedef std::vector<std::string> StringsVector;

        bpo::options_description desc = OpenMW::makeOptionsDescription();
        bpo::variables_map variables;

        Files::parseArgs(argc, argv, variables, desc);
        bpo::notify(variables);

        if (variables.contains("help"))
        {
            Debug::getRawStdout() << desc << std::endl;
            return false;
        }

        if (variables.contains("version"))
        {
            Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
            return false;
        }

        cfgMgr.processPaths(variables, std::filesystem::current_path());

        cfgMgr.readConfiguration(variables, desc);

        Debug::setupLogging(cfgMgr.getLogPath(), "OpenMW");
        Debug::setCrashReports(cfgMgr.getUserDataPath());
        Log(Debug::Info) << Version::getOpenmwVersionDescription();

        Settings::Manager::load(cfgMgr);
        Crash::setHangLimit(std::chrono::seconds(Settings::general().mCrashHangSeconds));

        MWGui::DebugWindow::startLogRecording();

        engine.setGrabMouse(!variables["no-grab"].as<bool>());

        // Font encoding settings
        std::string encoding(variables["encoding"].as<std::string>());
        Log(Debug::Info) << ToUTF8::encodingUsingMessage(encoding);
        engine.setEncoding(ToUTF8::calculateEncoding(encoding));

        Files::PathContainer dataDirs(asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));

        // This call to u8string is redundant, but required to build on MSVC 14.26 due to implementation bugs.
        Files::PathContainer::value_type local(
            variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>().u8string());
        if (!local.empty())
            dataDirs.push_back(std::move(local));

        cfgMgr.filterOutNonExistingPaths(dataDirs);

        // This call to u8string is redundant, but required to build on MSVC 14.26 due to implementation bugs.
        engine.setResourceDir(variables["resources"].as<Files::MaybeQuotedPath>().u8string());
        engine.setDataDirs(dataDirs);

        // fallback archives
        StringsVector archives = variables["fallback-archive"].as<StringsVector>();
        for (const auto& archive : archives)
        {
            engine.addArchive(archive);
        }

        StringsVector content = variables["content"].as<StringsVector>();
        if (content.empty())
        {
            Log(Debug::Error) << "No content file given (esm/esp, nor omwgame/omwaddon). Aborting...";
            return false;
        }
        engine.addContentFile("builtin.omwscripts");
        std::set<std::string> contentDedupe{ "builtin.omwscripts" };
        for (const auto& contentFile : content)
        {
            if (!contentDedupe.insert(contentFile).second)
            {
                Log(Debug::Error) << "Content file specified more than once: " << contentFile << ". Aborting...";
                return false;
            }
        }

        for (auto& file : content)
        {
            engine.addContentFile(file);
        }

        StringsVector groundcover = variables["groundcover"].as<StringsVector>();
        for (auto& file : groundcover)
        {
            engine.addGroundcoverFile(file);
        }

        if (variables.contains("lua-scripts"))
        {
            Log(Debug::Warning)
                << "Lua scripts have been specified via the old lua-scripts option and will not be loaded. "
                   "Please update them to a version which uses the new omwscripts format.";
        }

        // startup-settings
        engine.setCell(variables["start"].as<std::string>());
        engine.setSkipMenu(variables["skip-menu"].as<bool>(), variables["new-game"].as<bool>());
        if (!variables["skip-menu"].as<bool>() && variables["new-game"].as<bool>())
            Log(Debug::Warning) << "Warning: new-game used without skip-menu -> ignoring it";

        // scripts
        engine.setCompileAll(variables["script-all"].as<bool>());
        engine.setCompileAllDialogue(variables["script-all-dialogue"].as<bool>());
        engine.setScriptConsoleMode(variables["script-console"].as<bool>());
        engine.setStartupScript(variables["script-run"].as<std::string>());
        engine.setWarningsMode(variables["script-warn"].as<std::int32_t>());
        engine.setSaveGameFile(variables["load-savegame"].as<Files::MaybeQuotedPath>().u8string());

        // other settings
        Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);
        engine.setSoundUsage(!variables["no-sound"].as<bool>());
        engine.setActivationDistanceOverride(variables["activate-dist"].as<std::int32_t>());
        engine.enableFontExport(variables["export-fonts"].as<bool>());
        engine.setRandomSeed(variables["random-seed"].as<std::uint32_t>());

        return true;
    }

    class OSGLogHandler : public osg::NotifyHandler
    {
        void notify(osg::NotifySeverity severity, const char* msg) override
        {
            // Copy, because osg logging is not thread safe.
            std::string msgCopy(msg);
            if (msgCopy.empty())
                return;

            const Debug::Level level = [&] {
                switch (severity)
                {
                    case osg::ALWAYS:
                    case osg::FATAL:
                        return Debug::Error;
                    case osg::WARN:
                    case osg::NOTICE:
                        return Debug::Warning;
                    case osg::INFO:
                        return Debug::Info;
                    case osg::DEBUG_INFO:
                    case osg::DEBUG_FP:
                    default:
                        return Debug::Debug;
                }
            }();
            std::string_view s(msgCopy);
            if (s.size() < 1024)
                Log(level) << (s.back() == '\n' ? s.substr(0, s.size() - 1) : s);
            else
            {
                while (!s.empty())
                {
                    std::size_t lineSize = 1;
                    while (lineSize < s.size() && s[lineSize - 1] != '\n')
                        lineSize++;
                    Log(level) << s.substr(0, s[lineSize - 1] == '\n' ? lineSize - 1 : lineSize);
                    s = s.substr(lineSize);
                }
            }
        }
    };

    std::int32_t runApplication(std::int32_t argc, char* argv[])
    {
        Platform::init();

        osg::setNotifyHandler(new OSGLogHandler());
        Files::ConfigurationManager cfgMgr;
        std::unique_ptr<OMW::Engine> engine = std::make_unique<OMW::Engine>(cfgMgr);

        engine->setRecastMaxLogLevel(Debug::getRecastMaxLogLevel());

        if (parseOptions(argc, argv, *engine, cfgMgr))
            engine->go();

        return 0;
    }
}

int main(std::int32_t argc, char** argv)
{
    return Debug::wrapApplication(&runApplication, argc, argv, "OpenMW");
}

// Platform specific for Windows when there is no console built into the executable.
// Windows will call the WinMain function instead of main in this case, the normal
// main function is then called with the __argc and __argv parameters.
#if defined(_WIN32) && !defined(_CONSOLE)
std::int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, std::int32_t nShowCmd)
{
    return main(__argc, __argv);
}
#endif
