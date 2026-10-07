#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/rtxtool/model/maprules.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "film.hpp"
#include "verbs.hpp"

namespace Files
{
    struct ConfigurationManager;
}

namespace RtxTool
{
    /// Which commands read one option.
    struct OptionOwner
    {
        std::string_view mName;
        Verbs mVerbs = Verbs::Every;
    };

    /// Every option the harness takes, and which commands read each. In the library rather than
    /// beside `main`, because a `variables_map` is only usable once notified against the
    /// description that declares its keys, and a test that stands a world up needs the same one.
    struct ToolOptions
    {
        boost::program_options::options_description mDescription;

        /// One entry per option that only some commands read, in the order they are declared: the
        /// same statement the help line is printed from, so a command cannot take an option and
        /// throw it away.
        std::vector<OptionOwner> mOwners;

        /// Which commands read `name`. Every one of them where nothing said otherwise, which is
        /// only the options `Files::ConfigurationManager::addCommonOptions` adds.
        Verbs readsOption(std::string_view name) const;

        /// What `verb` was given on `line` and does not read, as the lines to print, or empty. The
        /// command line only: an option in `openmw.cfg` is there for every command.
        std::string complainAbout(const boost::program_options::parsed_options& line, Verbs verb) const;

        /// What `noise --versus=<switch>=<value>` runs its other side with: `played` with the one
        /// switch `asked` names read at its value, as the line would have read it, and every other
        /// switch of `sReconstructionSwitches` as `variables` reads it. A switch named alone takes
        /// its implicit value. Throws `std::runtime_error` for a switch no reconstruction reads,
        /// and for a value the switch refuses.
        Rtx::ReconstructionRequest versus(const boost::program_options::variables_map& variables,
            const Rtx::ReconstructionRequest& played, std::string_view asked) const;
    };

    /// `validationByDefault` is what `--validation` reads when nobody names it — a decision about the
    /// command line, which only the executable has: `sync` outside a Release build, `off` in one.
    ToolOptions makeOptions(Rtx::ValidationLevel validationByDefault);

    /// How long a film is to be and who said so, or nothing where `--speed` is named: `--length`, or
    /// `FilmPacing::sLengthByDefault` where neither is named, which `planFilm` sets aside where the
    /// keys leave it no frame to fill. Throws `std::runtime_error` for both named at once, and for a
    /// length that is none.
    std::optional<FilmLength> filmLengthFrom(const boost::program_options::variables_map& variables);

    /// Which companion maps `verb`'s models take: `--maps`, or where it is not given, nothing for the
    /// player's own under `view`, which is the played game, and `MapRules::Shipped` under every other
    /// verb. Throws `Rtx::InputError` for a spelling `sMapRulesNames` has not.
    std::optional<MapRules> mapRulesFrom(const boost::program_options::variables_map& variables, Verbs verb);

    /// The switches `readReconstruction` reads, by their names on the line: the ones `--versus`
    /// may name, since nothing else reads them.
    inline constexpr std::array sReconstructionSwitches = std::to_array<std::string_view>({ "filter", "jitter", "noise",
        "level-epsilon", "shadow-floor", "lamp-candidates", "antilag", "history-fix", "dual-motion", "antifirefly" });

    /// Writes into `request` what each of `sReconstructionSwitches` says, `noise` at `auto` as the
    /// request's own default. Throws `std::runtime_error` for a noise source no reconstruction
    /// has. The indirect light is not among them: a run derives it from the
    /// settings as well as the line.
    void readReconstruction(
        const boost::program_options::variables_map& variables, Rtx::ReconstructionRequest& request);

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
