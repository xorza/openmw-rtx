#pragma once

#include <components/esm3/creaturestate.hpp>

#include "importcrec.hpp"

namespace ESSImport
{

    void convertCREC(const CREC& crec, ESM::CreatureState& state);

}
