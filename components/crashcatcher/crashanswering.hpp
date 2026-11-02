#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace Crash
{
    /// The player answers the monitor's two boxes: the hang's "Wait or End", and the report a crash
    /// or an End leaves.
    struct AskThePlayer
    {
        bool operator==(const AskThePlayer&) const = default;
    };

    /// Nobody answers, so no box is shown: a game started from a shell, or a harness. A hang is
    /// reported, and the game left to draw again or end by itself.
    struct AskNobody
    {
        bool operator==(const AskNobody&) const = default;
    };

    /// **A harness's stand-in at the hang's box, where nobody is at it**: End, `mDelay` after
    /// `mFrom`, and no box after it either. What lets a test end a game that recovered, or ended,
    /// while it was asked.
    struct EndAfter
    {
        /// What `mDelay` counts from.
        enum class From
        {
            /// The ask, as a player's click does: an End that may land while the hang report the
            /// watch asked for is still written.
            Ask,

            /// The end of that report. **The game's own time**: the report holds the thread that
            /// takes it, and Crashpad every other while it reads them, for as long as the dump takes,
            /// which is seconds under a sanitizer. A delay from the ask spent that time as well, and
            /// ended a game that would have recovered or ended by itself.
            Report,
        };

        std::chrono::milliseconds mDelay{};
        From mFrom = From::Ask;

        bool operator==(const EndAfter&) const = default;

        /// One word, `end-after-ask-<milliseconds>` or `end-after-report-<milliseconds>`, which
        /// `read` reads back: what the game hands its monitor, and a harness the game.
        std::string spell() const;

        /// What `spell` wrote, or nothing for any other text.
        static std::optional<EndAfter> read(std::string_view spelled);
    };

    /// Who answers the monitor's boxes, one of the three.
    using Answering = std::variant<AskThePlayer, AskNobody, EndAfter>;
}
