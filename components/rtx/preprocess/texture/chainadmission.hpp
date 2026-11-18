#pragma once

#include "chainscratch.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// Whether a chain whose making holds `scratch` at most is made of `file`: not where it needs more than the system
    /// has left (`Platform::availableMemory`), which is logged. The one rule the loading screen's preparation and
    /// play's chains are both held to, asked only where a chain is to be made and not found.
    bool admitsChain(const ChainScratch& scratch, const osg::Image& file);
}
