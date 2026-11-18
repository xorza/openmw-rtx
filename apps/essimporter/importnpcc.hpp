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

    struct NPCC
    {
        struct NPDT
        {
            std::uint8_t mDisposition;
            std::uint8_t unknown;
            std::uint8_t mReputation;
            std::uint8_t unknown2;
            std::int32_t mIndex;
        } mNPDT{};

        Inventory mInventory;
        ESM::AIPackageList mAiPackages;

        void load(ESM::ESMReader& esm);
    };

}
