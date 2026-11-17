#pragma once

#include <cstdint>
#include <vector>

#include <components/esm/esmcommon.hpp>
#include <components/esm/vector3.hpp>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    struct PROJ
    {

        struct PNAM // 184 bytes
        {
            float mAttackStrength;
            float mSpeed;
            std::uint8_t mUnknown[4 * 2];
            float mFlightTime;
            std::int32_t mSplmIndex; // reference to a SPLM record (0 for ballistic projectiles)
            std::uint8_t mUnknown2[4];
            ESM::Vector3 mVelocity;
            ESM::Vector3 mPosition;
            std::uint8_t mUnknown3[4 * 9];
            ESM::NAME32 mActorId; // indexed refID (with the exception of "PlayerSaveGame")
            ESM::NAME32 mArrowId;
            ESM::NAME32 mBowId;

            bool isMagic() const { return mSplmIndex != 0; }
        };

        std::vector<PNAM> mProjectiles;

        void load(ESM::ESMReader& esm);
    };

}
