#pragma once

#include <components/esm3/inventorystate.hpp>

#include "importinventory.hpp"

namespace ESSImport
{

    void convertInventory(const Inventory& inventory, ESM::InventoryState& state);

}
