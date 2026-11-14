#pragma once

#include "importcrec.hpp"

#include <components/esm3/creaturestate.hpp>

namespace ESSImport
{

    void convertCREC(const CREC& crec, ESM::CreatureState& state);

}
