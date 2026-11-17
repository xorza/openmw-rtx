#include "contacttestresultcallback.hpp"

#include <cstdint>

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>

#include <components/misc/convert.hpp>

#include "ptrholder.hpp"

namespace MWPhysics
{
    btScalar ContactTestResultCallback::addSingleResult(btManifoldPoint& cp, const btCollisionObjectWrapper* col0Wrap,
        std::int32_t /*partId0*/, std::int32_t /*index0*/, const btCollisionObjectWrapper* col1Wrap,
        std::int32_t /*partId1*/, std::int32_t /*index1*/)
    {
        const btCollisionObject* collisionObject = col0Wrap->m_collisionObject;
        if (collisionObject == mTestedAgainst)
            collisionObject = col1Wrap->m_collisionObject;
        auto* holder = static_cast<PtrHolder*>(collisionObject->getUserPointer());
        if (holder)
            mResult.emplace_back(ContactPoint{ holder->getPtr(), Misc::Convert::toOsg(cp.m_positionWorldOnB),
                Misc::Convert::toOsg(cp.m_normalWorldOnB) });
        return 0.f;
    }

}
