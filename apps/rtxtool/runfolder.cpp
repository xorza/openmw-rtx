#include "runfolder.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <boost/program_options/variables_map.hpp>

#include <apps/rtxtool/instruments/wholenumber.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/process.hpp>

namespace RtxTool
{
    namespace bpo = boost::program_options;

    std::filesystem::path ownConfigDirectory(const Files::ConfigurationManager& config)
    {
        static const std::string run = std::format(
            "{}-{}", std::chrono::system_clock::now().time_since_epoch().count(), Platform::Process::currentId());
        return config.getCachePath() / "rtxtool" / run;
    }

    std::filesystem::path ownSavesFolder(const Files::ConfigurationManager& config)
    {
        return ownConfigDirectory(config) / "saves";
    }

    void sweepEndedRuns(const std::filesystem::path& runs)
    {
        // Listed whole before anything goes, as `DriverCache::sweep` says why.
        const std::optional<std::vector<std::filesystem::directory_entry>> listed = Platform::listFolder(runs);
        if (!listed.has_value())
            return;

        std::error_code failed;
        for (const std::filesystem::directory_entry& entry : *listed)
        {
            if (!entry.is_directory(failed))
                continue;

            // `<time>-<process id>`, and the whole of it: a name that is anything else is not a run's.
            const std::string name = Files::pathToUnicodeString(entry.path().filename());
            const std::size_t dash = name.find('-');
            if (dash == std::string::npos || dash == 0)
                continue;

            const std::string_view spelled = name;
            const std::optional<std::uint64_t> started = wholeNumber<std::uint64_t>(spelled.substr(0, dash));
            const std::optional<std::uint32_t> process = wholeNumber<std::uint32_t>(spelled.substr(dash + 1));
            if (!started.has_value() || !process.has_value())
                continue;

            if (!Platform::Process::isRunning(*process))
                std::filesystem::remove_all(entry.path(), failed);
        }
    }

    void adoptConfigDirectory(bpo::variables_map& variables, const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);

        // **The map's own entry and not a second parse.** `config` is a composing option, so a value
        // stored from a second source would be merged by rules that are Boost's to keep; the container
        // the first parse left is appended to directly, and `readConfiguration` reads it as it finds it.
        //
        // Present whenever the line was parsed against `ConfigurationManager::addCommonOptions`,
        // because `store` writes a defaulted value for every option of its description that the line
        // left out — so a map without it was parsed against the wrong description.
        const auto found = variables.find("config");
        Crash::contract(found != variables.end(), "the variables were not parsed against the engine's common options");

        found->second.as<Files::MaybeQuotedPathContainer>().push_back(Files::MaybeQuotedPath{ directory });
    }
}
