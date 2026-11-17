#include "heightfield.hpp"

#include <cstdint>

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <LinearMath/btTransform.h>

#include <components/bullethelpers/heightfield.hpp>

#include "mtphysics.hpp"

namespace MWPhysics
{
    HeightField::HeightField(const float* heights, std::int32_t x, std::int32_t y, std::int32_t size,
        std::int32_t verts, float minH, float maxH, std::shared_ptr<const ESMTerrain::LandObject> holdObject,
        PhysicsTaskScheduler* scheduler)
        : mHoldObject(std::move(holdObject))
        , mTaskScheduler(scheduler)
    {
        mShape = std::make_unique<btHeightfieldTerrainShape>(verts, verts, heights, minH, maxH, 2, false);
        mShape->setUseDiamondSubdivision(true);

        const float scaling = static_cast<float>(size) / static_cast<float>(verts - 1);
        mShape->setLocalScaling(btVector3(scaling, scaling, 1));

        // Accelerates some collision tests.
        //
        // Note: The accelerator data structure in Bullet is only used
        // in some operations. This could be improved, see:
        // https://github.com/bulletphysics/bullet3/issues/3276
        mShape->buildAccelerator();

        const btTransform transform(
            btQuaternion::getIdentity(), BulletHelpers::getHeightfieldShift(x, y, size, minH, maxH));

        mCollisionObject = std::make_unique<btCollisionObject>();
        mCollisionObject->setCollisionShape(mShape.get());
        mCollisionObject->setWorldTransform(transform);
        mTaskScheduler->addCollisionObject(
            mCollisionObject.get(), CollisionType_HeightMap, CollisionType_Actor | CollisionType_Projectile);
    }

    HeightField::~HeightField()
    {
        mTaskScheduler->removeCollisionObject(mCollisionObject.get());
    }

    btCollisionObject* HeightField::getCollisionObject()
    {
        return mCollisionObject.get();
    }

    const btCollisionObject* HeightField::getCollisionObject() const
    {
        return mCollisionObject.get();
    }

    const btHeightfieldTerrainShape* HeightField::getShape() const
    {
        return mShape.get();
    }
}
