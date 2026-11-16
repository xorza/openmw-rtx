#pragma once

#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <osg/BoundingBox>

#include <components/debug/debuglog.hpp>
#include <components/nif/niffile.hpp>
#include <components/resource/bulletshape.hpp>

class btTriangleMesh;
class btCompoundShape;
class btCollisionShape;

namespace Nif
{
    struct NiAVObject;
    struct NiNode;
    struct NiGeometry;
    struct Parent;
}

namespace NifBullet
{

    /**
     *Load bulletShape from NIF files.
     */
    class BulletNifLoader
    {
    public:
        void warn(const std::string& msg) { Log(Debug::Warning) << "NIFLoader: Warn: " << msg; }

        [[noreturn]] void fail(const std::string& msg)
        {
            Log(Debug::Error) << "NIFLoader: Fail: " << msg;
            abort();
        }

        std::shared_ptr<Resource::BulletShape> load(Nif::FileView nif);

    private:
        bool findBoundingBox(const Nif::NiAVObject& node);

        struct HandleNodeArgs
        {
            bool mHasMarkers{ false };
            bool mHasTriMarkers{ false };
            bool mAnimated{ false };
            bool mGenerateCollision{ false };
            bool mAvoid{ false };
            const Nif::NiNode* mCollisionNode{ nullptr };
        };

        void handleRoot(Nif::FileView nif, const Nif::NiAVObject& node, HandleNodeArgs args);
        void handleNode(const Nif::NiAVObject& node, const Nif::Parent* parent, HandleNodeArgs args);
        void handleGeometry(const Nif::NiGeometry& niGeometry, const Nif::Parent* parent, HandleNodeArgs args);

        std::unique_ptr<btCompoundShape, Resource::DeleteCollisionShape> mCompoundShape;
        std::unique_ptr<btCompoundShape, Resource::DeleteCollisionShape> mAvoidCompoundShape;

        std::shared_ptr<Resource::BulletShape> mShape;
    };

}
