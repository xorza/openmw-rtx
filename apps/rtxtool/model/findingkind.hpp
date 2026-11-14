#pragma once

#include <cstdint>

namespace RtxTool
{
    /// What a tour met. A fault fails the run; a cost is reported and ranked, and fails it only where the line asks
    /// (`--fail-on`); a mark is the watcher's own.
    enum class FindingKind : std::uint8_t
    {
        /// A shader stored a value that was not finite (`Rtx::NotFinite`), by the module that wrote it.
        NotFinite,

        /// The content gave something the renderer could not use (`Rtx::Refusals`), by kind; the log names it.
        Refused,

        /// A claim of `check` the leg could answer failed at its end.
        CheckFailed,

        /// The log said an error.
        LogError,

        /// A cell the leg covers never stood whole while the player was in it: the flight outran the streaming.
        Outrun,

        /// A leg could not go on: the world never stood whole, a pause nobody closed.
        LegFailed,

        /// A frame over the budget that took one and a half times the frame before it.
        Hitch,

        /// A leg's frames over the budget, counted once a leg with its worst.
        SlowFrames,

        /// The log said a warning.
        LogWarning,

        /// What the closing leg came to past where the first leg began: memory or the median frame.
        Growth,

        /// Where the watcher pressed Home.
        Mark,

        Count,
    };
}
