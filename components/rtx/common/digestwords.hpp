#pragma once

#include <array>
#include <cstdint>

namespace Rtx
{
    /// A hundred and twenty-eight bits that name what was hashed: `MurmurHash3_x64_128`'s state as
    /// the content keys, the shader tree's digest and the harness hold it, and an image's every bit
    /// as `shaders/digest.h` folds them. One name, so a digest handed from one to another is one
    /// type.
    using DigestWords = std::array<std::uint64_t, 2>;
}
