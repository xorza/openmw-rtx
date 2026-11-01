#pragma once

#include <filesystem>

#include <boost/program_options/variables_map.hpp>

namespace Files
{
    struct ConfigurationManager;
}

namespace RtxTool
{
    /// Where the engine's own state goes when this tool drives it: the settings it saves on its
    /// way out, its log, its key bindings, its Lua storage. Under the cache path, because every
    /// byte of it is regenerable.
    ///
    /// **A directory of this run's own**, named `<time>-<process id>` under `rtxtool`, and none a
    /// run before it wrote into: the settings a run saves are its overrides, and a run that read
    /// them back as the user layer took a `shot`'s distant land for its own. One directory for every
    /// run was also one for two runs at once — two checkouts, two flavours — each loading the
    /// other's settings and storage. The same path for every call in one process.
    std::filesystem::path ownConfigDirectory(const Files::ConfigurationManager& config);

    /// Where the game saves while this tool drives it: inside the run's own directory, so the
    /// player's saves folder never holds a run's world — god mode, every skill at its top, and the
    /// harness's own scripts in the content list. F5 in a played `view` replaced the quicksave of the
    /// character a `--load-savegame` had loaded.
    std::filesystem::path ownSavesFolder(const Files::ConfigurationManager& config);

    /// Removes the directory of every run in `runs` whose process has ended, and nothing else:
    /// not a run still going, and nothing not named as `ownConfigDirectory` names one. At the next
    /// run's start and not at a run's own end, so the log of the last run stays to be read until
    /// another begins, and nothing is removed while its process still holds the log open.
    void sweepEndedRuns(const std::filesystem::path& runs);

    /// Makes `directory` the last configuration directory of the run, and creates it. The last,
    /// because `Settings::Manager::load` reads that one as the user layer the engine writes back
    /// to: with the player's own directory last, `bench --distant-statics=false` left the played
    /// game with its object paging off. Before `Files::ConfigurationManager::readConfiguration`.
    void adoptConfigDirectory(boost::program_options::variables_map& variables, const std::filesystem::path& directory);
}
