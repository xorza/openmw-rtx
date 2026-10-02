#include "startup.hpp"

#include <chrono>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashinstall.hpp>
#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/version/version.hpp>

#include "engine.hpp"

namespace OpenMW
{
    void startLogAndSettings(const Files::ConfigurationManager& config, std::string_view application)
    {
        Debug::setupLogging(config.getLogPath(), application);
        Debug::setCrashReports(config.getUserDataPath());
        Log(Debug::Info) << Version::getOpenmwVersionDescription();
        Crash::annotate("version", Version::getOpenmwVersionDescription());

        Settings::Manager::load(config);
        Crash::setHangLimit(std::chrono::seconds(Settings::general().mCrashHangSeconds));
    }

    void Installation::handTo(OMW::Engine& engine) const
    {
        Log(Debug::Info) << ToUTF8::encodingUsingMessage(mEncoding);
        engine.setEncoding(ToUTF8::calculateEncoding(mEncoding));
        engine.setDataDirs(mDataDirs);
        for (const std::string& archive : mArchives)
            engine.addArchive(archive);
        for (const std::string& file : mContent)
            engine.addContentFile(file);
        for (const std::string& file : mGroundcover)
            engine.addGroundcoverFile(file);
        Fallback::Map::init(mFallback.mMap);
    }

    Misc::Result<Installation, std::string> readInstallation(const boost::program_options::variables_map& variables,
        const Files::ConfigurationManager& config, const InstallationExtras& extras)
    {
        using StringsVector = std::vector<std::string>;

        Installation installation{
            .mEncoding = variables["encoding"].as<std::string>(),
            .mDataDirs = Files::asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()),
            .mArchives = variables["fallback-archive"].as<StringsVector>(),
            .mContent = {},
            .mGroundcover = variables["groundcover"].as<StringsVector>(),
            .mFallback = variables["fallback"].as<Fallback::FallbackMap>(),
        };

        if (Files::PathContainer::value_type local(variables["data-local"].as<Files::MaybeQuotedPath>().u8string());
            !local.empty())
            installation.mDataDirs.push_back(std::move(local));
        config.filterOutNonExistingPaths(installation.mDataDirs);
        installation.mDataDirs.insert(installation.mDataDirs.end(), extras.mDataDirs.begin(), extras.mDataDirs.end());

        const StringsVector& content = variables["content"].as<StringsVector>();
        if (content.empty())
            return Misc::Err{ std::string("No content file given (esm/esp, nor omwgame/omwaddon)") };

        installation.mContent.reserve(1 + extras.mContent.size() + content.size());
        installation.mContent.emplace_back("builtin.omwscripts");
        installation.mContent.insert(installation.mContent.end(), extras.mContent.begin(), extras.mContent.end());

        std::set<std::string> once(installation.mContent.begin(), installation.mContent.end());
        for (const std::string& file : content)
        {
            if (!once.insert(file).second)
                return Misc::Err{ "Content file specified more than once: " + file };
            installation.mContent.push_back(file);
        }

        return installation;
    }
}
