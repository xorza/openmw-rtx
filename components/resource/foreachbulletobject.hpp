#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <osg/ref_ptr>

#include <components/esm/position.hpp>

#include "bulletshape.hpp"

namespace ESM
{
    class ReadersCache;
    struct Cell;
}

namespace VFS
{
    class Manager;
}

namespace Resource
{
    class BulletShapeManager;
}

namespace EsmLoader
{
    struct EsmData;
}

namespace Resource
{
    struct BulletObject
    {
        std::shared_ptr<const Resource::BulletShape> mShape;
        ESM::Position mPosition;
        float mScale;
    };

    void forEachBulletObject(ESM::ReadersCache& readers, const VFS::Manager& vfs,
        Resource::BulletShapeManager& bulletShapeManager, const EsmLoader::EsmData& esmData,
        std::function<void(const ESM::Cell&, const BulletObject& object)> callback);
}
