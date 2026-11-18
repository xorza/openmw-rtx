#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/refiddata.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadscpt.hpp>

namespace CSMWorld
{
    class Resources;
}

namespace CSMDoc
{
    class Messages;
}

namespace CSMTools
{
    class ReferenceableCheckStage : public CSMDoc::Stage
    {
    public:
        ReferenceableCheckStage(const CSMWorld::RefIdData& referenceable,
            const CSMWorld::IdCollection<ESM::Race>& races, const CSMWorld::IdCollection<ESM::Class>& classes,
            const CSMWorld::IdCollection<ESM::Faction>& factions, const CSMWorld::IdCollection<ESM::Script>& scripts,
            const CSMWorld::Resources& models, const CSMWorld::Resources& icons,
            const CSMWorld::IdCollection<ESM::BodyPart>& bodyparts);

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        std::int32_t setup() override;

    private:
        // CONCRETE CHECKS
        void bookCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Book>& records, CSMDoc::Messages& messages);
        void activatorCheck(std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Activator>& records,
            CSMDoc::Messages& messages);
        void potionCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Potion>& records, CSMDoc::Messages& messages);
        void apparatusCheck(std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Apparatus>& records,
            CSMDoc::Messages& messages);
        void armorCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Armor>& records, CSMDoc::Messages& messages);
        void clothingCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Clothing>& records, CSMDoc::Messages& messages);
        void containerCheck(std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Container>& records,
            CSMDoc::Messages& messages);
        void creatureCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Creature>& records, CSMDoc::Messages& messages);
        void doorCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Door>& records, CSMDoc::Messages& messages);
        void ingredientCheck(std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Ingredient>& records,
            CSMDoc::Messages& messages);
        void creaturesLevListCheck(std::int32_t stage,
            const CSMWorld::RefIdDataContainer<ESM::CreatureLevList>& records, CSMDoc::Messages& messages);
        void itemLevelledListCheck(std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::ItemLevList>& records,
            CSMDoc::Messages& messages);
        void lightCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Light>& records, CSMDoc::Messages& messages);
        void lockpickCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Lockpick>& records, CSMDoc::Messages& messages);
        void miscCheck(std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Miscellaneous>& records,
            CSMDoc::Messages& messages);
        void npcCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::NPC>& records, CSMDoc::Messages& messages);
        void weaponCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Weapon>& records, CSMDoc::Messages& messages);
        void probeCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Probe>& records, CSMDoc::Messages& messages);
        void repairCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Repair>& records, CSMDoc::Messages& messages);
        void staticCheck(
            std::int32_t stage, const CSMWorld::RefIdDataContainer<ESM::Static>& records, CSMDoc::Messages& messages);

        // FINAL CHECK
        void finalCheck(CSMDoc::Messages& messages);

        // Convenience functions
        void inventoryListCheck(
            const std::vector<ESM::ContItem>& itemList, CSMDoc::Messages& messages, const std::string& id);

        /// for all enchantable items.
        template <typename Item>
        inline void inventoryItemCheck(
            const Item& someItem, CSMDoc::Messages& messages, const std::string& someID, bool enchantable);

        /// for non-enchantable items.
        template <typename Item>
        inline void inventoryItemCheck(const Item& someItem, CSMDoc::Messages& messages, const std::string& someID);

        /// for tools with uses.
        template <typename Tool>
        inline void toolCheck(
            const Tool& someTool, CSMDoc::Messages& messages, const std::string& someID, bool canbebroken);

        /// for tools without uses.
        template <typename Tool>
        inline void toolCheck(const Tool& someTool, CSMDoc::Messages& messages, const std::string& someID);

        template <typename List>
        inline void listCheck(const List& someList, CSMDoc::Messages& messages, const std::string& someID);

        template <typename Tool>
        inline void scriptCheck(const Tool& someTool, CSMDoc::Messages& messages, const std::string& someID);

        const CSMWorld::RefIdData& mReferencables;
        const CSMWorld::IdCollection<ESM::Race>& mRaces;
        const CSMWorld::IdCollection<ESM::Class>& mClasses;
        const CSMWorld::IdCollection<ESM::Faction>& mFactions;
        const CSMWorld::IdCollection<ESM::Script>& mScripts;
        const CSMWorld::Resources& mModels;
        const CSMWorld::Resources& mIcons;
        const CSMWorld::IdCollection<ESM::BodyPart>& mBodyParts;
        bool mPlayerPresent;
        bool mIgnoreBaseRecords;
    };
}
