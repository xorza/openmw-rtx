#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options/variables_map.hpp>

#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/result.hpp>

namespace OMW
{
    class Engine;
}

namespace OpenMW
{
    /// Opens the log, loads the settings and hands the crash catcher what it reads of both: the
    /// version every report carries and how long without a frame is a hang. Once the configuration
    /// chain is read, in every binary that runs the engine, so a hang in the harness is reported as
    /// one in the game is.
    void startLogAndSettings(const Files::ConfigurationManager& config, std::string_view application);

    /// What a binary adds to the installation its configuration describes: data folders after the
    /// configured ones, and content files between `builtin.omwscripts` and the configured ones.
    struct InstallationExtras
    {
        Files::PathContainer mDataDirs;
        std::vector<std::string> mContent;
    };

    /// The installation a binary runs, as its configuration and its command line describe it
    /// (`Files::addInstallationOptions`): one reading for the game and every binary that runs it.
    struct Installation
    {
        std::string mEncoding;

        /// The configured data folders that exist, the local one last, then the extras'.
        Files::PathContainer mDataDirs;

        std::vector<std::string> mArchives;

        /// `builtin.omwscripts`, the extras' content, then the configured content.
        std::vector<std::string> mContent;

        std::vector<std::string> mGroundcover;

        Fallback::FallbackMap mFallback;

        /// Hands all of it to `engine`, and the fallback values to `Fallback::Map`.
        void handTo(OMW::Engine& engine) const;
    };

    /// The installation `variables` describe with `extras`, or why there is none: a content list
    /// that is empty or names a file twice.
    Misc::Result<Installation, std::string> readInstallation(const boost::program_options::variables_map& variables,
        const Files::ConfigurationManager& config, const InstallationExtras& extras);
}
