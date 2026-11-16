#include "platform.hpp"

#include <cstdint>
#include <type_traits>

#ifdef WIN32
#include <stdio.h>
#endif

namespace Platform
{
    // **The fixed widths are the keywords they replaced, on every target the tree builds for** (`AGENTS.md`,
    // "Fixed-width integers"): the rewrite changed no type, overload, format or ABI. A target where one differs stops
    // here, before a call resolves to another overload.
    static_assert(std::is_same_v<std::int8_t, signed char>); // NOLINT(google-runtime-int): the premise
    static_assert(std::is_same_v<std::uint8_t, unsigned char>); // NOLINT(google-runtime-int): the premise
    static_assert(std::is_same_v<std::int16_t, short>); // NOLINT(google-runtime-int): the premise
    static_assert(std::is_same_v<std::uint16_t, unsigned short>); // NOLINT(google-runtime-int): the premise
    static_assert(std::is_same_v<std::int32_t, int>); // NOLINT(google-runtime-int): the premise
    static_assert(std::is_same_v<std::uint32_t, unsigned int>); // NOLINT(google-runtime-int): the premise

    namespace
    {
        void increaseFileHandleLimit()
        {
#ifdef WIN32
            // Increase limit for open files at the stream I/O level, see
            // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/setmaxstdio?view=msvc-170#remarks
            _setmaxstdio(8192);
#else
            // No-op on any other platform.
#endif
        }
    }

    void init()
    {
        increaseFileHandleLimit();
    }

}
