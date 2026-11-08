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

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "toolline.hpp"
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

        /// The line `variables` holds, once the configuration filled it, read for `verb` into what
        /// each option means: every spelling refused here, with the option that named it, as a
        /// `std::runtime_error` or an `Rtx::InputError`. `--frames` beside `--seconds`, `--length`
        /// beside `--speed`, and a place named beside `--views` are refused as two answers to one
        /// question.
        ToolLine read(const boost::program_options::variables_map& variables, Verbs verb) const;

        /// What `noise --versus=<switch>=<value>` runs its other side with: `played` with the one
        /// switch `asked` names read at its value, as the line would have read it. A switch named
        /// alone takes its implicit value. Throws `std::runtime_error` for a switch no
        /// reconstruction reads, and for a value the switch refuses.
        Rtx::ReconstructionRequest versus(const Rtx::ReconstructionRequest& played, std::string_view asked) const;
    };

    /// `validationByDefault` is what `--validation` reads when nobody names it — a decision about the
    /// command line, which only the executable has: `sync` outside a Release build, `off` in one.
    ToolOptions makeOptions(Rtx::ValidationLevel validationByDefault);

    /// The reconstruction's switches, by their names on the line: the ones `--versus` may name, since
    /// nothing else reads them.
    inline constexpr std::array sReconstructionSwitches = std::to_array<std::string_view>(
        { "filter", "jitter", "noise", "level-epsilon", "shadow-floor", "lamp-candidates", "indirect", "bounce-rays",
            "relief", "antilag", "history-fix", "dual-motion", "antifirefly" });
}
