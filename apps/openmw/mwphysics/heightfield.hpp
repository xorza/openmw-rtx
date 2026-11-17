#pragma once

#include <cstdint>
#include <memory>

class btCollisionObject;
class btHeightfieldTerrainShape;

namespace ESMTerrain
{
    class LandObject;
}

namespace MWPhysics
{
    class PhysicsTaskScheduler;

    class HeightField
    {
    public:
        void operator=(const HeightField&) = delete;
        HeightField(const HeightField&) = delete;
        HeightField(const float* heights, std::int32_t x, std::int32_t y, std::int32_t size, std::int32_t verts,
            float minH, float maxH, std::shared_ptr<const ESMTerrain::LandObject> holdObject,
            PhysicsTaskScheduler* scheduler);
        ~HeightField();

        btCollisionObject* getCollisionObject();
        const btCollisionObject* getCollisionObject() const;
        const btHeightfieldTerrainShape* getShape() const;

    private:
        std::unique_ptr<btHeightfieldTerrainShape> mShape;
        std::unique_ptr<btCollisionObject> mCollisionObject;
        std::shared_ptr<const ESMTerrain::LandObject> mHoldObject;

        PhysicsTaskScheduler* mTaskScheduler;
    };
}
