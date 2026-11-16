#pragma once

#include <components/esm3/npcstate.hpp>

#include "importnpcc.hpp"

namespace ESSImport
{

    void convertNPCC(const NPCC& npcc, ESM::NpcState& npcState);

}
