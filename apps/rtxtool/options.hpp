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

#include <apps/rtxtool/model/benchspec.hpp>
#include <apps/rtxtool/model/maprules.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "film.hpp"
#include "verbs.hpp"

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

    /// How long every stop of a run lasts: `--frames` where it is named, and `--seconds` otherwise.
    /// Throws `std::runtime_error` for both named at once, which are two answers to one question.
    BenchSpan benchSpanFrom(const boost::program_options::variables_map& variables);

    /// Which companion maps `verb`'s models take: `--maps`, or where it is not given, nothing for the
    /// player's own under `view`, which is the played game, and `MapRules::Shipped` under every other
    /// verb. Throws `Rtx::InputError` for a spelling `sMapRulesNames` has not.
    std::optional<MapRules> mapRulesFrom(const boost::program_options::variables_map& variables, Verbs verb);

    /// The switches `readReconstruction` reads, by their names on the line: the ones `--versus`
    /// may name, since nothing else reads them.
    inline constexpr std::array sReconstructionSwitches
        = std::to_array<std::string_view>({ "filter", "jitter", "noise", "level-epsilon", "shadow-floor",
            "lamp-candidates", "bounce-rays", "antilag", "history-fix", "dual-motion", "antifirefly" });

    /// Writes into `request` what each of `sReconstructionSwitches` says, `noise` at `auto` as the
    /// request's own default. Throws `std::runtime_error` for a noise source no reconstruction
    /// has.
    void readReconstruction(
        const boost::program_options::variables_map& variables, Rtx::ReconstructionRequest& request);
}
