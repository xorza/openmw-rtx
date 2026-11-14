#pragma once

#include <BulletCollision/Gimpact/btBoxCollision.h>

#include <cstddef>

namespace DetourNavigator
{
    class CommulativeAabb
    {
    public:
        explicit CommulativeAabb(std::size_t lastChangeRevision, const btAABB& aabb);

        bool update(std::size_t lastChangeRevision, const btAABB& aabb);

    private:
        std::size_t mLastChangeRevision;
        btAABB mAabb;
    };
}
