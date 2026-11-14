#pragma once

#include "importcntc.hpp"

#include <components/esm3/containerstate.hpp>

namespace ESSImport
{

    void convertCNTC(const CNTC& cntc, ESM::ContainerState& state);

}
