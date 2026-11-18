#pragma once

#include <components/esm3/containerstate.hpp>

#include "importcntc.hpp"

namespace ESSImport
{

    void convertCNTC(const CNTC& cntc, ESM::ContainerState& state);

}
