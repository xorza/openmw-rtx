#pragma once

#include <cstdint>
#include <string>

#include "importscri.hpp"

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    enum ACDTFlags
    {
        TalkedToPlayer = 0x4,
        Attacked = 0x100,
        Unknown = 0x200
    };
    enum ACSCFlags
    {
        Dead = 0x2
    };

    /// Actor data, shared by (at least) REFR and CellRef
    struct ACDT
    {
        // Note, not stored at *all*:
        // - Level changes are lost on reload, except for the player (there it's in the NPC record).
        std::uint8_t mUnknown[12];
        std::uint32_t mFlags;
        float mBreathMeter; // Seconds left before drowning
        std::uint8_t mUnknown2[20];
        float mDynamic[3][2];
        std::uint8_t mUnknown3[16];
        float mAttributes[8][2];
        float mMagicEffects[27]; // Effect attributes:
                                 // https://wiki.openmw.org/index.php?title=Research:Magic#Effect_attributes
        std::uint8_t mUnknown4[4];
        std::uint32_t mGoldPool;
        std::uint8_t mCountDown; // seen the same value as in ACSC.mCorpseClearCountdown, maybe
                                 // this one is for respawning?
        std::uint8_t mUnknown5[3];
    };
    struct ACSC
    {
        std::uint8_t mUnknown1[17];
        std::uint8_t mFlags; // ACSCFlags
        std::uint8_t mUnknown2[22];
        std::uint8_t mCorpseClearCountdown; // hours?
        std::uint8_t mUnknown3[71];
    };
    struct ANIS
    {
        std::uint8_t mGroupIndex;
        std::uint8_t mUnknown[3];
        float mTime;
    };

    struct ActorData
    {
        bool mHasACDT{};
        ACDT mACDT{};

        bool mHasACSC{};
        ACSC mACSC{};

        std::int32_t mSkills[27][2]{}; // skills, base and modified

        // creature combat stats, base and modified
        // I think these can be ignored in the conversion, because it is not possible
        // to change them ingame
        std::int32_t mCombatStats[3][2]{};

        ESM::RefId mSelectedSpell;
        ESM::RefId mSelectedEnchantItem;

        SCRI mSCRI;

        bool mHasANIS{};
        ANIS mANIS{}; // scripted animation state
    };

}
