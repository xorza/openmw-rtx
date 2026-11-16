#include "bulletshapemanager.hpp"

#include <cstdint>
#include <memory>

#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btScaledBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btStridingMeshInterface.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btTriangleMeshShape.h>
#include <osg/Drawable>
#include <osg/NodeVisitor>
#include <osg/Transform>
#include <osg/TriangleFunctor>

#include <components/misc/convert.hpp>
#include <components/misc/osguservalues.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/nifbullet/bulletnifloader.hpp>
#include <components/sceneutil/visitor.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "bulletshape.hpp"
#include "multiobjectcache.hpp"
#include "niffilemanager.hpp"
#include "objectcache.hpp"
#include "scenemanager.hpp"

namespace Resource
{

    namespace
    {
        /// The bytes of `shape`'s triangles, its vertices and its indices, in every part of it; not its bounding
        /// volume tree, which Bullet builds beside them and states no size of.
        std::uint64_t meshBytes(const btCollisionShape& shape)
        {
            switch (shape.getShapeType())
            {
                case COMPOUND_SHAPE_PROXYTYPE:
                {
                    const auto& compound = static_cast<const btCompoundShape&>(shape);
                    std::uint64_t bytes = 0;
                    for (int child = 0; child < compound.getNumChildShapes(); ++child)
                        bytes += meshBytes(*compound.getChildShape(child));
                    return bytes;
                }
                case SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE:
                    return meshBytes(*static_cast<const btScaledBvhTriangleMeshShape&>(shape).getChildShape());
                case TRIANGLE_MESH_SHAPE_PROXYTYPE:
                {
                    const btStridingMeshInterface& mesh
                        = *static_cast<const btTriangleMeshShape&>(shape).getMeshInterface();
                    std::uint64_t bytes = 0;
                    for (int part = 0; part < mesh.getNumSubParts(); ++part)
                    {
                        const unsigned char* vertices = nullptr;
                        int vertexCount = 0;
                        PHY_ScalarType vertexType{};
                        int vertexStride = 0;
                        const unsigned char* indices = nullptr;
                        int indexStride = 0;
                        int triangleCount = 0;
                        PHY_ScalarType indexType{};
                        mesh.getLockedReadOnlyVertexIndexBase(&vertices, vertexCount, vertexType, vertexStride,
                            &indices, indexStride, triangleCount, indexType, part);
                        bytes += static_cast<std::uint64_t>(vertexCount) * static_cast<std::uint64_t>(vertexStride)
                            + static_cast<std::uint64_t>(triangleCount) * static_cast<std::uint64_t>(indexStride);
                        mesh.unLockReadOnlyVertexBase(part);
                    }
                    return bytes;
                }
                default:
                    return 0;
            }
        }

        std::uint64_t shapeBytes(const BulletShape& shape)
        {
            std::uint64_t bytes = 0;
            for (const CollisionShapePtr* part : { &shape.mCollisionShape, &shape.mAvoidCollisionShape })
                if (*part != nullptr)
                    bytes += meshBytes(**part);
            return bytes;
        }

        struct GetTriangleFunctor
        {
            GetTriangleFunctor()
                : mTriMesh(nullptr)
            {
            }

            void setTriMesh(btTriangleMesh* triMesh) { mTriMesh = triMesh; }

            void setMatrix(const osg::Matrixf& matrix) { mMatrix = matrix; }

            void inline operator()(const osg::Vec3& v1, const osg::Vec3& v2, const osg::Vec3& v3,
                bool /*temp*/ = false) // Note: unused temp argument left here for OSG versions less than 3.5.6
            {
                if (mTriMesh)
                    mTriMesh->addTriangle(Misc::Convert::toBullet(mMatrix.preMult(v1)),
                        Misc::Convert::toBullet(mMatrix.preMult(v2)), Misc::Convert::toBullet(mMatrix.preMult(v3)));
            }

            btTriangleMesh* mTriMesh;
            osg::Matrixf mMatrix;
        };

        /// Creates a BulletShape out of a Node hierarchy.
        class NodeToShapeVisitor : public osg::NodeVisitor
        {
        public:
            NodeToShapeVisitor()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mTriangleMesh(nullptr)
            {
            }

            void apply(osg::Drawable& drawable) override
            {
                if (!mTriangleMesh)
                    mTriangleMesh = std::make_unique<btTriangleMesh>();

                osg::Matrixf worldMat = osg::computeLocalToWorld(getNodePath());
                osg::TriangleFunctor<GetTriangleFunctor> functor;
                functor.setTriMesh(mTriangleMesh.get());
                functor.setMatrix(worldMat);
                drawable.accept(functor);
            }

            std::shared_ptr<BulletShape> getShape()
            {
                if (!mTriangleMesh || mTriangleMesh->getNumTriangles() == 0)
                    return std::shared_ptr<BulletShape>();

                auto shape = std::make_shared<BulletShape>();

                auto triangleMeshShape = std::make_unique<TriangleMeshShape>(mTriangleMesh.release(), true);
                btVector3 aabbMin = triangleMeshShape->getLocalAabbMin();
                btVector3 aabbMax = triangleMeshShape->getLocalAabbMax();
                shape->mCollisionBox.mExtents[0] = static_cast<float>(aabbMax[0] - aabbMin[0]) / 2.0f;
                shape->mCollisionBox.mExtents[1] = static_cast<float>(aabbMax[1] - aabbMin[1]) / 2.0f;
                shape->mCollisionBox.mExtents[2] = static_cast<float>(aabbMax[2] - aabbMin[2]) / 2.0f;
                shape->mCollisionBox.mCenter = osg::Vec3f(static_cast<float>(aabbMax[0] + aabbMin[0]) / 2.0f,
                    static_cast<float>(aabbMax[1] + aabbMin[1]) / 2.0f,
                    static_cast<float>(aabbMax[2] + aabbMin[2]) / 2.0f);
                shape->mCollisionShape.reset(triangleMeshShape.release());

                return shape;
            }

        private:
            std::unique_ptr<btTriangleMesh> mTriangleMesh;
        };
    }

    BulletShapeManager::BulletShapeManager(
        const VFS::Manager* vfs, SceneManager* sceneMgr, NifFileManager* nifFileManager, CacheBudget& budget)
        : ResourceManager(vfs, budget)
        , mInstanceCache(std::make_unique<MultiObjectCache<std::shared_ptr<BulletShapeInstance>>>())
        , mSceneManager(sceneMgr)
        , mNifFileManager(nifFileManager)
    {
    }

    BulletShapeManager::~BulletShapeManager() = default;

    std::shared_ptr<const BulletShape> BulletShapeManager::getShape(VFS::Path::NormalizedView name)
    {
        return mCache->getOrLoad(name.value(), [&] { return load(name); });
    }

    Loaded<std::shared_ptr<const BulletShape>> BulletShapeManager::load(VFS::Path::NormalizedView name)
    {
        std::shared_ptr<BulletShape> shape;

        if (Misc::getFileExtension(name.value()) == "nif")
        {
            NifBullet::BulletNifLoader loader;
            shape = loader.load(*mNifFileManager->get(name));
        }
        else
        {
            // TODO: support .bullet shape files

            osg::ref_ptr<const osg::Node> constNode(mSceneManager->getTemplate(name));
            // const-trickery required because there is no const version of NodeVisitor
            osg::ref_ptr<osg::Node> node(const_cast<osg::Node*>(constNode.get()));

            // Check first if there's a custom collision node
            unsigned int visitAllNodesMask = 0xffffffff;
            SceneUtil::FindByNameVisitor nameFinder("Collision");
            nameFinder.setTraversalMask(visitAllNodesMask);
            nameFinder.setNodeMaskOverride(visitAllNodesMask);
            node->accept(nameFinder);
            if (nameFinder.mFoundNode)
            {
                NodeToShapeVisitor visitor;
                visitor.setTraversalMask(visitAllNodesMask);
                visitor.setNodeMaskOverride(visitAllNodesMask);
                nameFinder.mFoundNode->accept(visitor);
                shape = visitor.getShape();
            }

            // Generate a collision shape from the mesh
            if (!shape)
            {
                NodeToShapeVisitor visitor;
                node->accept(visitor);
                shape = visitor.getShape();
                if (!shape)
                    return {};
            }

            if (shape != nullptr)
            {
                shape->mFileName = name;
                constNode->getUserValue(Misc::OsgUserValues::sFileHash, shape->mFileHash);
            }
        }

        const std::uint64_t bytes = shape == nullptr ? 0 : shapeBytes(*shape);
        return { .mValue = std::move(shape), .mBytes = bytes };
    }

    std::shared_ptr<BulletShapeInstance> BulletShapeManager::cacheInstance(VFS::Path::NormalizedView name)
    {
        std::shared_ptr<BulletShapeInstance> instance = createInstance(name);
        if (instance != nullptr)
            mInstanceCache->addEntryToObjectCache(name, instance);
        return instance;
    }

    std::shared_ptr<BulletShapeInstance> BulletShapeManager::getInstance(VFS::Path::NormalizedView name)
    {
        if (std::shared_ptr<BulletShapeInstance> cached = mInstanceCache->takeFromObjectCache(name))
            return cached;
        return createInstance(name);
    }

    std::shared_ptr<BulletShapeInstance> BulletShapeManager::createInstance(VFS::Path::NormalizedView name)
    {
        if (std::shared_ptr<const BulletShape> shape = getShape(name))
            return makeInstance(std::move(shape));
        return std::shared_ptr<BulletShapeInstance>();
    }

    void BulletShapeManager::updateCache(double referenceTime)
    {
        ResourceManager<std::shared_ptr<const BulletShape>>::updateCache(referenceTime);

        mInstanceCache->removeUnreferencedObjectsInCache();
    }

    void BulletShapeManager::clearCache()
    {
        ResourceManager<std::shared_ptr<const BulletShape>>::clearCache();

        mInstanceCache->clear();
    }

    void BulletShapeManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Shape", frameNumber, mCache->getStats(), *stats);
        Resource::reportStats("Shape Instance", frameNumber, mInstanceCache->getStats(), *stats);
    }

}
