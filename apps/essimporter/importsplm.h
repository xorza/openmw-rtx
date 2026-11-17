#pragma once

#include <cstdint>
#include <vector>

#include <components/esm/esmcommon.hpp>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    struct SPLM
    {

        struct SPDT // 160 bytes
        {
            std::int32_t mType; // 1 = spell, 2 = enchantment, 3 = potion
            ESM::NAME32 mId; // base ID of a spell/enchantment/potion
            std::uint8_t mUnknown[4 * 4];
            ESM::NAME32 mCasterId;
            ESM::NAME32 mSourceId; // empty for spells
            std::uint8_t mUnknown2[4 * 11];
        };

        struct NPDT // 56 bytes
        {
            ESM::NAME32 mAffectedActorId;
            std::uint8_t mUnknown[4 * 2];
            std::int32_t mMagnitude;
            float mSecondsActive;
            std::uint8_t mUnknown2[4 * 2];
        };

        struct INAM // 40 bytes
        {
            std::int32_t mUnknown;
            std::uint8_t mUnknown2;
            ESM::FixedString<35> mItemId; // disintegrated item / bound item / item to re-equip after expiration
        };

        struct CNAM // 36 bytes
        {
            std::int32_t mUnknown; // seems to always be 0
            ESM::NAME32 mSummonedOrCommandedActor[32];
        };

        struct VNAM // 4 bytes
        {
            std::int32_t mUnknown;
        };

        struct ActiveEffect
        {
            NPDT mNPDT;
        };

        struct ActiveSpell
        {
            std::int32_t mIndex{};
            SPDT mSPDT{};
            std::string mTarget;
            std::vector<ActiveEffect> mActiveEffects;
        };

        std::vector<ActiveSpell> mActiveSpells;

        void load(ESM::ESMReader& esm);
    };

}
