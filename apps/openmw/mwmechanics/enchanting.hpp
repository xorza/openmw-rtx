#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadench.hpp>

namespace MWMechanics
{
    class Enchanting
    {
        MWWorld::Ptr mOldItemPtr;
        MWWorld::Ptr mSoulGemPtr;
        MWWorld::Ptr mEnchanter;

        std::int32_t mCastStyle;

        bool mSelfEnchanting;

        ESM::EffectList mEffectList;

        std::string mNewItemName;
        std::uint32_t mObjectType;
        ESM::RefId mWeaponType;

        const ESM::Enchantment* getRecord(const ESM::Enchantment& toFind) const;
        std::int32_t getBaseCastCost() const; // To be saved in the enchantment's record
        std::int32_t getEnchantItemsCount() const;
        float getTypeMultiplier() const;
        void payForEnchantment(std::int32_t count) const;
        std::int32_t getEnchantPrice(std::int32_t count) const;
        std::vector<float> getEffectCosts() const;

    public:
        Enchanting();
        void setEnchanter(const MWWorld::Ptr& enchanter);
        void setSelfEnchanting(bool selfEnchanting);
        void setOldItem(const MWWorld::Ptr& oldItem);
        MWWorld::Ptr getOldItem() { return mOldItemPtr; }
        MWWorld::Ptr getGem() { return mSoulGemPtr; }
        void setNewItemName(const std::string& s);
        void setEffect(const ESM::EffectList& effectList);
        void setSoulGem(const MWWorld::Ptr& soulGem);
        bool create(); // Return true if created, false if failed.
        void nextCastStyle(); // Set enchant type to next possible type (for mOldItemPtr object)
        std::int32_t getCastStyle() const;
        float getEnchantPoints(bool precise = true) const;
        std::int32_t getEffectiveCastCost()
            const; // Effective cost taking player Enchant skill into account, used for preview purposes in the UI
        std::int32_t getEnchantPrice() const { return getEnchantPrice(getEnchantItemsCount()); }
        std::int32_t getMaxEnchantValue() const;
        std::int32_t getGemCharge() const;
        std::int32_t getEnchantChance() const;
        bool soulEmpty() const; // Return true if empty
        bool itemEmpty() const; // Return true if empty
    };
}
