#pragma once

namespace RtxTool
{
    /// What a run of the harness ends with, which `tools/omw/exitstatus.py` mirrors: a driver tells "the run
    /// broke" from "the run found a difference" by this number and not by the report's wording, which a reworded
    /// sentence would swap.
    ///
    /// Listed from the best to the worst, which is how two parts of one run are told apart (`worseOf`).
    enum class ExitStatus : int
    {
        Passed = 0,

        /// Did everything it was asked, and something it was held to differed: a hashed frame from its reference,
        /// a picture from `--against`'s, a frame noisier than its frames averaged.
        Differed = 3,

        /// Could not do what it was asked, or a check it asked failed.
        Failed = 1,

        /// The line named what the tool does not have: a command nobody is called, an option nobody declared or
        /// one the command does not read. 2, as POSIX's utilities and Python's argparse answer a usage error.
        Refused = 2,
    };

    /// The worse of two parts of one run: a refusal over a failure over a difference over a pass.
    constexpr ExitStatus worseOf(const ExitStatus a, const ExitStatus b)
    {
        const auto rank = [](const ExitStatus status) {
            switch (status)
            {
                case ExitStatus::Passed:
                    return 0;
                case ExitStatus::Differed:
                    return 1;
                case ExitStatus::Failed:
                    return 2;
                case ExitStatus::Refused:
                    return 3;
            }
            return 3;
        };

        return rank(a) >= rank(b) ? a : b;
    }

    /// The process's exit code for `status`.
    constexpr int codeOf(const ExitStatus status)
    {
        return static_cast<int>(status);
    }
}
