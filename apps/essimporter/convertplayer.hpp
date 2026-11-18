#pragma once

#include <components/esm3/controlsstate.hpp>
#include <components/esm3/player.hpp>

#include "importplayer.hpp"

namespace ESSImport
{

    void convertPCDT(const PCDT& pcdt, ESM::Player& out, std::vector<ESM::RefId>& outDialogueTopics,
        bool& firstPersonCam, bool& teleportingEnabled, bool& levitationEnabled, ESM::ControlsState& controls);

}
