#pragma once

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

namespace MWPhysics
{
    struct ContactTestWrapper
    {
        static void contactTest(btCollisionWorld* collisionWorld, btCollisionObject* colObj,
            btCollisionWorld::ContactResultCallback& resultCallback);
        static void contactPairTest(btCollisionWorld* collisionWorld, btCollisionObject* colObjA,
            btCollisionObject* colObjB, btCollisionWorld::ContactResultCallback& resultCallback);
    };
}
