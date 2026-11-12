#pragma once

#include <cassert>
#include <cstdint>

namespace Rtx
{
    /// The `index`th term of the radical inverse in `base`: write the index in that base and reflect its
    /// digits about the point. In base two it is Van der Corput's sequence.
    ///
    /// **Correctly rounded in every base**: the digits are reversed as a whole number and divided by `baseⁿ`
    /// once, both exact in a double, and the double's quotient rounds to the float's nearest, since 53 bits
    /// are more than twice 24 and two. Dividing a float at every digit rounded at each step for base three.
    inline float radicalInverse(std::uint32_t index, std::uint32_t base)
    {
        assert(base >= 2 && base < (1u << 16) && "a base whose powers past a 32-bit index stay exact in a double");

        std::uint64_t reversed = 0;
        std::uint64_t scale = 1;
        while (index > 0)
        {
            reversed = reversed * base + index % base;
            scale *= base;
            index /= base;
        }

        return static_cast<float>(static_cast<double>(reversed) / static_cast<double>(scale));
    }
}
