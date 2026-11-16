#pragma once

#include <type_traits>
#include <utility>

#include <BulletCollision/CollisionShapes/btTriangleCallback.h>

namespace BulletHelpers
{
    template <class Impl>
    class ProcessTriangleCallback : public btTriangleCallback
    {
    public:
        explicit ProcessTriangleCallback(Impl impl)
            : mImpl(std::move(impl))
        {
        }

        void processTriangle(btVector3* triangle, int partId, int triangleIndex) override
        {
            return mImpl(triangle, partId, triangleIndex);
        }

    private:
        Impl mImpl;
    };

    template <class Impl>
    ProcessTriangleCallback<typename std::decay<Impl>::type> makeProcessTriangleCallback(Impl&& impl)
    {
        return ProcessTriangleCallback<typename std::decay<Impl>::type>(std::forward<Impl>(impl));
    }
}
