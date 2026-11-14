#pragma once

#include "importinventory.hpp"

#include <components/esm3/inventorystate.hpp>

namespace ESSImport
{

    void convertInventory(const Inventory& inventory, ESM::InventoryState& state);

}
