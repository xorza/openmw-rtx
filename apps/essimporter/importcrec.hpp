#pragma once

#include <cstdint>

#include <components/esm3/aipackage.hpp>

#include "importinventory.hpp"

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Creature changes
    struct CREC
    {
        std::int32_t mIndex{};

        Inventory mInventory;
        ESM::AIPackageList mAiPackages;

        void load(ESM::ESMReader& esm);
    };

}
