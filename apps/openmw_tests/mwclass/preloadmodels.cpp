#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/vfs/pathutil.hpp>

#include "apps/openmw/mwclass/preloadmodels.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"

namespace MWClass
{
    namespace
    {
        std::vector<std::string> namesOf(const std::vector<VFS::Path::NormalizedView>& models)
        {
            std::vector<std::string> names;
            for (const VFS::Path::NormalizedView model : models)
                names.emplace_back(model.value());
            return names;
        }

        /// **An NPC the game never met is preloaded in what its record carries**: a shirt with a male and a
        /// female part, and a cuirass with a male part alone, list the female shirt and the male cuirass for
        /// a woman, the male of each for a man; a levelled item and an item of no part list nothing.
        TEST(MWClassPreloadModelsTest, anUnmetNpcListsThePartsItsRecordWears)
        {
            MWWorld::ESMStore store;
            const auto part = [&](std::string_view id, std::string_view model) {
                ESM::BodyPart record;
                record.blank();
                record.mId = ESM::RefId::stringRefId(id);
                record.mModel = std::string(model);
                store.insertStatic(record);
            };
            part("shirt_m", "meshes/c/shirt_m.nif");
            part("shirt_f", "meshes/c/shirt_f.nif");
            part("cuirass_m", "meshes/a/cuirass_m.nif");

            ESM::Clothing shirt;
            shirt.blank();
            shirt.mId = ESM::RefId::stringRefId("shirt");
            shirt.mParts.mParts.push_back(ESM::PartReference{ .mPart = 8,
                .mMale = ESM::RefId::stringRefId("shirt_m"),
                .mFemale = ESM::RefId::stringRefId("shirt_f") });
            store.insertStatic(shirt);
            ESM::Armor cuirass;
            cuirass.blank();
            cuirass.mId = ESM::RefId::stringRefId("cuirass");
            cuirass.mParts.mParts.push_back(
                ESM::PartReference{ .mPart = 1, .mMale = ESM::RefId::stringRefId("cuirass_m"), .mFemale = {} });
            store.insertStatic(cuirass);

            ESM::NPC npc;
            npc.blank();
            npc.mInventory.mList = { ESM::ContItem{ .mCount = 1, .mItem = ESM::RefId::stringRefId("shirt") },
                ESM::ContItem{ .mCount = 1, .mItem = ESM::RefId::stringRefId("cuirass") },
                ESM::ContItem{ .mCount = 1, .mItem = ESM::RefId::stringRefId("l_random_armor") } };

            const auto listed = [&](bool female) {
                npc.mFlags = female ? ESM::NPC::Female : 0;
                std::vector<VFS::Path::NormalizedView> models;
                appendRecordWornParts(store, npc, models);
                return namesOf(models);
            };
            EXPECT_EQ(listed(true), (std::vector<std::string>{ "meshes/c/shirt_f.nif", "meshes/a/cuirass_m.nif" }));
            EXPECT_EQ(listed(false), (std::vector<std::string>{ "meshes/c/shirt_m.nif", "meshes/a/cuirass_m.nif" }));
        }

        /// **A levelled list is preloaded in every creature it may spawn, each list walked once**: the
        /// top list names a rat, a nested list twice and itself, and the nested list a mudcrab, the top
        /// list again, a creature of no model and an id nothing holds. The rat and the mudcrab, once
        /// each: a walk of each naming, as deep as the content goes, listed the mudcrab twice at the
        /// first step and on through every loop.
        TEST(MWClassPreloadModelsTest, aLevelledListListsEachCreatureItMaySpawnWalkingEachListOnce)
        {
            MWWorld::ESMStore store;
            const auto creature = [&](std::string_view id, std::string_view model) {
                ESM::Creature record;
                record.blank();
                record.mId = ESM::RefId::stringRefId(id);
                record.mModel = std::string(model);
                store.insertStatic(record);
            };
            creature("rat", "meshes/r/rat.nif");
            creature("mudcrab", "meshes/r/mudcrab.nif");
            creature("ghost", "");

            const auto item = [](std::string_view id) {
                return ESM::LevelledListBase::LevelItem{ .mId = ESM::RefId::stringRefId(id), .mLevel = 1 };
            };
            const auto list = [&](std::string_view id, std::vector<ESM::LevelledListBase::LevelItem> items) {
                ESM::CreatureLevList record;
                record.blank();
                record.mId = ESM::RefId::stringRefId(id);
                record.mList = std::move(items);
                store.insertStatic(record);
            };
            list("l_top", { item("rat"), item("l_mid"), item("l_top"), item("l_mid") });
            list("l_mid", { item("mudcrab"), item("l_top"), item("ghost"), item("nothing") });

            const ESM::CreatureLevList* const top
                = store.get<ESM::CreatureLevList>().search(ESM::RefId::stringRefId("l_top"));
            ASSERT_NE(top, nullptr);
            std::vector<VFS::Path::NormalizedView> models;
            appendLevelledCreatureModels(store, *top, models);
            EXPECT_EQ(namesOf(models), (std::vector<std::string>{ "meshes/r/rat.nif", "meshes/r/mudcrab.nif" }));
        }
    }
}
