#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace Crash
{
    /// What `fatal` does in a process no catcher watches: `reason` to the standard error, and an
    /// abort all the same. One body for the build without Crashpad and the process that did not
    /// install it.
    [[noreturn]] inline void abortUncaught(std::string_view reason)
    {
        static_cast<void>(
            std::fprintf(stderr, "Fatal: %.*s\n", static_cast<std::int32_t>(reason.size()), reason.data()));
        std::abort();
    }
}
