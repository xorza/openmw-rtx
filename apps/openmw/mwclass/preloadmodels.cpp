#include "preloadmodels.hpp"

#include <algorithm>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadnpc.hpp>

#include "../mwworld/esmstore.hpp"

namespace MWClass
{
    void appendPartModels(const MWWorld::ESMStore& store, const std::vector<ESM::PartReference>& parts,
        const bool female, std::vector<VFS::Path::NormalizedView>& models)
    {
        for (const ESM::PartReference& partRef : parts)
        {
            const ESM::RefId& partname = (female && !partRef.mFemale.empty()) || (!female && partRef.mMale.empty())
                ? partRef.mFemale
                : partRef.mMale;

            const ESM::BodyPart* part = store.get<ESM::BodyPart>().search(partname);
            if (part && !part->mModel.empty())
                models.push_back(part->mModel.getNormalized());
        }
    }

    void appendRecordWornParts(
        const MWWorld::ESMStore& store, const ESM::NPC& npc, std::vector<VFS::Path::NormalizedView>& models)
    {
        const bool female = (npc.mFlags & ESM::NPC::Female) != 0;
        for (const ESM::ContItem& item : npc.mInventory.mList)
        {
            if (const ESM::Clothing* clothing = store.get<ESM::Clothing>().search(item.mItem))
                appendPartModels(store, clothing->mParts.mParts, female, models);
            else if (const ESM::Armor* armor = store.get<ESM::Armor>().search(item.mItem))
                appendPartModels(store, armor->mParts.mParts, female, models);
        }
    }

    void appendLevelledCreatureModels(const MWWorld::ESMStore& store, const ESM::CreatureLevList& list,
        std::vector<VFS::Path::NormalizedView>& models)
    {
        // The lists met, each walked once in the order met: bounded by the lists the content holds, and
        // a loop and not a recursion, since a chain of lists is as deep as content makes it.
        std::vector<const ESM::CreatureLevList*> lists{ &list };
        for (std::size_t at = 0; at < lists.size(); ++at)
            for (const ESM::LevelledListBase::LevelItem& item : lists[at]->mList)
            {
                if (const ESM::Creature* creature = store.get<ESM::Creature>().search(item.mId))
                {
                    if (!creature->mModel.empty())
                        models.push_back(creature->mModel.getNormalized());
                }
                else if (const ESM::CreatureLevList* nested = store.get<ESM::CreatureLevList>().search(item.mId);
                    nested != nullptr && std::ranges::find(lists, nested) == lists.end())
                    lists.push_back(nested);
            }
    }
}
