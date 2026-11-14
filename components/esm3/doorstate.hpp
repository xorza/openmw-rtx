#pragma once

#include "objectstate.hpp"

namespace ESM
{
    // format 0, saved games only

    struct DoorState final : public ObjectState
    {
        int32_t mDoorState = 0;

        void load(ESMReader& esm) override;
        void save(ESMWriter& esm, bool inInventory = false) const override;

        DoorState& asDoorState() override { return *this; }
        const DoorState& asDoorState() const override { return *this; }
    };
}
