#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <components/esm/esmcommon.hpp>
#include <components/esm3/cellref.hpp>

#include "importscri.hpp"

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    struct ContItem
    {
        int32_t mCount;
        ESM::NAME32 mItem;
    };

    struct Inventory
    {
        struct InventoryItem : public ESM::CellRef
        {
            std::string mId;
            int32_t mCount;
            int32_t mRelativeEquipmentSlot;
            SCRI mSCRI;
        };
        std::vector<InventoryItem> mItems;

        void load(ESM::ESMReader& esm);
    };

}
