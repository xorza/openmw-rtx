#pragma once

namespace Rtx
{
    /// Whether this build keeps `assert`: what says a figure is a debug build's. The standard's
    /// own switch, which is defined or not rather than nought or one, read here once.
#ifdef NDEBUG
    inline constexpr bool sAssertsOn = false;
#else
    inline constexpr bool sAssertsOn = true;
#endif
}
