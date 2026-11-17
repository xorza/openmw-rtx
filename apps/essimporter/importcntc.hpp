#pragma once

#include <cstdint>

#include "importinventory.hpp"

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Changed container contents
    struct CNTC
    {
        std::int32_t mIndex{};

        Inventory mInventory;

        void load(ESM::ESMReader& esm);
    };

}
