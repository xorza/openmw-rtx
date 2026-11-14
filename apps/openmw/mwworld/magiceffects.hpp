#pragma once

namespace ESM
{
    struct CreatureStats;
    struct InventoryState;
    struct NpcStats;
}

namespace MWWorld
{
    void convertMagicEffects(
        ESM::CreatureStats& creatureStats, ESM::InventoryState& inventory, ESM::NpcStats* npcStats = nullptr);

    void convertStats(ESM::CreatureStats& creatureStats);

    void convertEnchantmentSlots(ESM::CreatureStats& creatureStats, ESM::InventoryState& inventory);
}
