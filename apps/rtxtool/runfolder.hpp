#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <boost/program_options/variables_map.hpp>

namespace Files
{
    struct ConfigurationManager;
}

namespace RtxTool
{
    /// Where the engine's own state goes when this tool drives it: the settings it saves on its way out, its log,
    /// its key bindings, its Lua storage. Under the cache path, because every byte of it is regenerable.
    ///
    /// A directory of this run's own, named `run` (`nameThisRun`) under `rtxtool`: the settings a run saves are its
    /// overrides, and a run that read them back as the user layer took a `shot`'s distant land for its own, and one
    /// directory for every run would have two runs at once load each other's settings and storage.
    std::filesystem::path ownConfigDirectory(const Files::ConfigurationManager& config, std::string_view run);

    /// This process's run, as its directory is named: `<time>-<process id>`, read off the clock where it is asked,
    /// so a run asks it once and hands it on.
    std::string nameThisRun();

    /// Where the game saves while this tool drives it: inside the run's own directory `own`, so the player's saves
    /// folder never holds a run's world (god mode, every skill at its top, the harness's own scripts in the
    /// content list).
    std::filesystem::path ownSavesFolder(const std::filesystem::path& own);

    /// Removes everything in `runs`, the folder `ownConfigDirectory` makes its directories in, but the directory of
    /// a run still going: an ended run's, and whatever an earlier layout of the folder left, file or directory. At
    /// the next run's start and not at a run's own end, so the log of the last run stays to be read until another
    /// begins, and nothing is removed while its process still holds the log open.
    void sweepAllButLiveRuns(const std::filesystem::path& runs);

    /// Makes `directory` the last configuration directory of the run, and creates it. The last, because
    /// `Settings::Manager::load` reads that one as the user layer the engine writes back to. Before
    /// `Files::ConfigurationManager::readConfiguration`.
    void adoptConfigDirectory(boost::program_options::variables_map& variables, const std::filesystem::path& directory);
}
