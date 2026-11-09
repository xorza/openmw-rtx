#pragma once

#include <cstdint>
#include <vector>

namespace Rtx
{
    /// The field `sFogNoise` holds, in its layout. Run by the build's tool (`fognoisetool.cpp`) and by
    /// the test that holds the compiled field to it.
    std::vector<std::uint8_t> makeFogNoise();
}
