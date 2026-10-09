#pragma once

#include <sys/types.h>

/// What the AppImage keeper reads of the system: Linux's alone, as the keeper is.
namespace Crash
{
    /// A process's parent, as its `/proc/<id>/stat` names it.
    struct ProcessStat
    {
        enum class Outcome
        {
            /// `mParent` holds the parent.
            Parented,
            /// The process was reaped after its `stat` was opened, which fails the read with `ESRCH`.
            Gone,
            /// Any other failure, or a text that names no parent.
            Unknown,
        };

        Outcome mOutcome = Outcome::Unknown;
        pid_t mParent = 0;
    };

    /// The parent the open `stat` at `descriptor` names, read from where it stands.
    ProcessStat readProcessStat(int descriptor) noexcept;
}
