#pragma once

#include "importnpcc.hpp"

#include <components/esm3/npcstate.hpp>

namespace ESSImport
{

    void convertNPCC(const NPCC& npcc, ESM::NpcState& npcState);

}
