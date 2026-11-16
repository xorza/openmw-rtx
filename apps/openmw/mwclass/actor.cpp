#include "actor.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/luamanager.hpp>
#include <apps/openmw/mwbase/soundmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/magiceffects.hpp>
#include <apps/openmw/mwmechanics/movement.hpp>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/esm3/loadmgef.hpp>

namespace MWClass
{
    void Actor::adjustPosition(const MWWorld::Ptr& ptr, bool force) const
    {
        MWBase::Environment::get().getWorld()->adjustPosition(ptr, force);
    }

    void Actor::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        physics.addActor(ptr, VFS::Path::toNormalized(model));
        if (getCreatureStats(ptr).isDead() && getCreatureStats(ptr).isDeathAnimationFinished())
            MWBase::Environment::get().getWorld()->enableActorCollision(ptr, false);
    }

    bool Actor::useAnim() const
    {
        return true;
    }

    osg::Vec3f Actor::getRotationVector(const MWWorld::Ptr& ptr) const
    {
        MWMechanics::Movement& movement = getMovementSettings(ptr);
        osg::Vec3f vec(movement.mRotation[0], movement.mRotation[1], movement.mRotation[2]);
        movement.mRotation[0] = 0.0f;
        movement.mRotation[1] = 0.0f;
        movement.mRotation[2] = 0.0f;
        return vec;
    }

    float Actor::getEncumbrance(const MWWorld::Ptr& ptr) const
    {
        float weight = getContainerStore(ptr).getWeight();
        const MWMechanics::MagicEffects& effects = getCreatureStats(ptr).getMagicEffects();
        weight -= effects.getOrDefault(MWMechanics::EffectKey(ESM::MagicEffect::Feather)).getMagnitude();
        if (ptr != MWMechanics::getPlayer() || !MWBase::Environment::get().getWorld()->getGodModeState())
            weight += effects.getOrDefault(MWMechanics::EffectKey(ESM::MagicEffect::Burden)).getMagnitude();
        return (weight < 0) ? 0.0f : weight;
    }

    bool Actor::allowTelekinesis(const MWWorld::ConstPtr& ptr) const
    {
        return false;
    }

    bool Actor::isActor() const
    {
        return true;
    }

    float Actor::getCurrentSpeed(const MWWorld::Ptr& ptr) const
    {
        const MWMechanics::Movement& movementSettings = ptr.getClass().getMovementSettings(ptr);
        float moveSpeed = this->getMaxSpeed(ptr) * movementSettings.mSpeedFactor;
        if (movementSettings.mIsStrafing)
            moveSpeed *= 0.75f;
        return moveSpeed;
    }

    bool Actor::consume(const MWWorld::Ptr& consumable, const MWWorld::Ptr& actor) const
    {
        MWMechanics::CastSpell cast(actor, actor);
        const ESM::RefId& recordId = consumable.getCellRef().getRefId();
        MWBase::Environment::get().getWorldModel()->registerPtr(consumable);
        MWBase::Environment::get().getLuaManager()->itemConsumed(consumable, actor);
        actor.getClass().getContainerStore(actor).remove(consumable, 1);
        if (cast.cast(recordId))
        {
            MWBase::Environment::get().getWorld()->breakInvisibility(actor);
            return true;
        }
        return false;
    }
}
