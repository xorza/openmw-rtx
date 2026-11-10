#pragma once

#include <vector>

namespace Rtx
{
    /// The tile `sBlueNoise` holds, by void-and-cluster rather than shipped as a mask, so a test can
    /// check that the ranks are a permutation and the spectrum has no low frequencies. Run by the
    /// build's tool (`bluenoisetool.cpp`) and by the test that holds the compiled tile to it.
    std::vector<float> makeBlueNoise();
}
