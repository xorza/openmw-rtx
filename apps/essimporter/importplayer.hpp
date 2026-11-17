#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <components/esm/esmcommon.hpp>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Other player data
    struct PCDT
    {
        std::int32_t mBounty{};
        std::string mBirthsign;

        std::vector<std::string> mKnownDialogueTopics;

        enum PlayerFlags
        {
            PlayerFlags_ViewSwitchDisabled = 0x1,
            PlayerFlags_ControlsDisabled = 0x4,
            PlayerFlags_Sleeping = 0x10,
            PlayerFlags_Waiting = 0x40,
            PlayerFlags_WeaponDrawn = 0x80,
            PlayerFlags_SpellDrawn = 0x100,
            PlayerFlags_InJail = 0x200,
            PlayerFlags_JumpingDisabled = 0x1000,
            PlayerFlags_LookingDisabled = 0x2000,
            PlayerFlags_VanityModeDisabled = 0x4000,
            PlayerFlags_WeaponDrawingDisabled = 0x8000,
            PlayerFlags_SpellDrawingDisabled = 0x10000,
            PlayerFlags_ThirdPerson = 0x20000,
            PlayerFlags_TeleportingDisabled = 0x40000,
            PlayerFlags_LevitationDisabled = 0x80000
        };

        struct FNAM
        {
            std::uint8_t mRank;
            std::uint8_t mUnknown1[3];
            std::int32_t mReputation;
            std::uint8_t mFlags; // 0x1: unknown, 0x2: expelled
            std::uint8_t mUnknown2[3];
            ESM::NAME32 mFactionName;
        };

        struct PNAM
        {
            struct MarkLocation
            {
                float mX, mY, mZ; // worldspace position
                float mRotZ; // Z angle in radians
                std::int32_t mCellX, mCellY; // grid coordinates; for interior cells this is always (0, 0)
            };

            struct Rotation
            {
                float mData[3][3];
            };

            std::int32_t mPlayerFlags; // controls, camera and draw state
            std::uint32_t mLevelProgress;
            float mSkillProgress[27]; // skill progress, non-uniform scaled
            std::uint8_t mSkillIncreases[8]; // number of skill increases for each attribute
            std::int32_t mTelekinesisRangeBonus; // in units; seems redundant
            float mVisionBonus; // range: <0.0, 1.0>; affected by light spells and Get/Mod/SetPCVisionBonus
            std::int32_t mDetectKeyMagnitude; // seems redundant
            std::int32_t mDetectEnchantmentMagnitude; // seems redundant
            std::int32_t mDetectAnimalMagnitude; // seems redundant
            MarkLocation mMarkLocation;
            std::uint8_t mUnknown3[4];
            Rotation mVerticalRotation;
            std::uint8_t mSpecIncreases[3]; // number of skill increases for each specialization
            std::uint8_t mUnknown4;
        };

        struct ENAM
        {
            std::int32_t mCellX;
            std::int32_t mCellY;
        };

        struct AADT // 44 bytes
        {
            std::int32_t animGroupIndex; // See convertANIS() for the mapping.
            std::uint8_t mUnknown5[40];
        };

        std::vector<FNAM> mFactions;
        PNAM mPNAM{};

        bool mHasMark{};
        std::string mMNAM; // mark cell name; can also be sDefaultCellname or region name

        bool mHasENAM{};
        ENAM mENAM{}; // last exterior cell

        bool mHasAADT{};
        AADT mAADT{};

        void load(ESM::ESMReader& esm);
    };

}
