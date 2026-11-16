#pragma once

#include "importinventory.hpp"
#include <components/esm3/aipackage.hpp>
#include <cstdint>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Creature changes
    struct CREC
    {
        int32_t mIndex{};

        Inventory mInventory;
        ESM::AIPackageList mAiPackages;

        void load(ESM::ESMReader& esm);
    };

}
