#pragma once

#include <osg/Quat>
#include <osg/Vec3f>

#include <components/esm/quaternion.hpp>
#include <components/esm/refid.hpp>
#include <components/esm/vector3.hpp>

#include "refnum.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, savegames only

    struct BaseProjectileState
    {
        RefId mId;

        Vector3 mPosition{};
        Quaternion mOrientation{};

        RefNum mCaster;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

    struct MagicBoltState : public BaseProjectileState
    {
        RefId mSpellId;
        float mSpeed{};
        RefNum mItem;

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void load(ESMReader& esm);
        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void save(ESMWriter& esm) const;
    };

    struct ProjectileState : public BaseProjectileState
    {
        RefId mBowId;
        Vector3 mVelocity{};
        float mAttackStrength{};
        float mAttackWindUp{};

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void load(ESMReader& esm);
        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void save(ESMWriter& esm) const;
    };

}
