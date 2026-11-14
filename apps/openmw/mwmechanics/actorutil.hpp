#pragma once

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    MWWorld::Ptr getPlayer();
    bool isPlayerInCombat();
    bool canActorMoveByZAxis(const MWWorld::Ptr& actor);
    bool hasWaterWalking(const MWWorld::Ptr& actor);
    bool isTargetMagicallyHidden(const MWWorld::Ptr& actor);
}
