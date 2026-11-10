#pragma once

#include <vector>

namespace Rtx
{
    /// The table `sSpecularAlbedo` holds, node by node: the integral of the lobe `shaders/brdf.h`
    /// draws, two values a node. Run by the build's tool (`specularalbedotool.cpp`) and by the test
    /// that holds the compiled table to it.
    std::vector<float> makeSpecularAlbedo();
}
