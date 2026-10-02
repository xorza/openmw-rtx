#include "startup.hpp"

#include <chrono>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashinstall.hpp>
#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>

namespace OpenMW
{
    void startLogAndSettings(const Files::ConfigurationManager& config, std::string_view application)
    {
        Debug::setupLogging(config.getLogPath(), application);
        Log(Debug::Info) << Version::getOpenmwVersionDescription();
        Crash::annotate("version", Version::getOpenmwVersionDescription());

        Settings::Manager::load(config);
        Crash::setHangLimit(std::chrono::seconds(Settings::general().mCrashHangSeconds));
    }
}
