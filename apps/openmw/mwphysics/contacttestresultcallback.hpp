#pragma once

#include <cstdint>
#include <vector>

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

#include "physicssystem.hpp"

class btCollisionObject;
struct btCollisionObjectWrapper;

namespace MWPhysics
{
    class ContactTestResultCallback : public btCollisionWorld::ContactResultCallback
    {
    public:
        explicit ContactTestResultCallback(const btCollisionObject* testedAgainst)
            : mTestedAgainst(testedAgainst)
        {
        }

        btScalar addSingleResult(btManifoldPoint& cp, const btCollisionObjectWrapper* col0Wrap, std::int32_t partId0,
            std::int32_t index0, const btCollisionObjectWrapper* col1Wrap, std::int32_t partId1,
            std::int32_t index1) override;

        std::vector<ContactPoint> mResult;

    private:
        const btCollisionObject* mTestedAgainst;
    };
}
