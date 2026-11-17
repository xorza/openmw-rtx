#pragma once

#include <cstddef>
#include <cstdint>

namespace CSMWorld
{
    namespace DefaultGmsts
    {

        const std::size_t FloatCount = 258;
        const std::size_t IntCount = 89;
        const std::size_t StringCount = 1174;

        const std::size_t OptionalFloatCount = 42;
        const std::size_t OptionalIntCount = 4;
        const std::size_t OptionalStringCount = 26;

        extern const char* Floats[];
        extern const char* Ints[];
        extern const char* Strings[];

        extern const char* OptionalFloats[];
        extern const char* OptionalInts[];
        extern const char* OptionalStrings[];

        extern const float FloatsDefaultValues[];
        extern const std::int32_t IntsDefaultValues[];

        extern const float FloatLimits[];
        extern const std::int32_t IntLimits[];

    }
}
