#pragma once

#include <vector>

#include <components/vfs/pathutil.hpp>

namespace ESM
{
    struct CreatureLevList;
    struct NPC;
    struct PartReference;
}

namespace MWWorld
{
    class ESMStore;
}

/// The models a cell's preloader is asked for out of a reference's record, for what the game builds
/// once it meets the reference and its record alone names.
namespace MWClass
{
    /// The models of `parts` as a body of `female` or not wears them: the part for its sex, and the
    /// other where the reference names none for it.
    void appendPartModels(const MWWorld::ESMStore& store, const std::vector<ESM::PartReference>& parts, bool female,
        std::vector<VFS::Path::NormalizedView>& models);

    /// The worn models of every clothing and armour item `npc`'s record carries: what an NPC the game
    /// never met wears, which has no inventory yet to name what it equips. Levelled items, which
    /// resolve once the NPC is met, name nothing.
    void appendRecordWornParts(
        const MWWorld::ESMStore& store, const ESM::NPC& npc, std::vector<VFS::Path::NormalizedView>& models);

    /// The models of every creature `list` may spawn, through the lists it names, each list walked
    /// once: which creature it spawns is drawn when its cell is met, and a list named twice or naming
    /// itself, which content may do, adds nothing more.
    void appendLevelledCreatureModels(const MWWorld::ESMStore& store, const ESM::CreatureLevList& list,
        std::vector<VFS::Path::NormalizedView>& models);
}
