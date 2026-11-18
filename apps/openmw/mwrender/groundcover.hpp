#pragma once

#include <cstdint>

#include <components/esm3/loadcell.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/terrain/quadtreeworld.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWWorld
{
    class ESMStore;
    class GroundcoverStore;
}
namespace osg
{
    class Program;
}

namespace MWRender
{
    typedef std::tuple<osg::Vec2f, float> GroundcoverChunkId; // Center, Size
    class Groundcover : public Resource::NodeResourceManager<GroundcoverChunkId>,
                        public Terrain::QuadTreeWorld::ChunkManager
    {
    public:
        Groundcover(Resource::SceneManager* sceneManager, float density, float viewDistance,
            const MWWorld::GroundcoverStore& store);
        ~Groundcover() override;

        osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f& center, std::uint8_t lod, std::uint32_t lodFlags,
            bool activeGrid, const osg::Vec3f& viewPoint, bool compile) override;

        std::uint32_t getNodeMask() override;

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override;

        struct GroundcoverEntry
        {
            ESM::Position mPos;
            float mScale;

            explicit GroundcoverEntry(const ESM::CellRef& ref)
                : mPos(ref.mPos)
                , mScale(ref.mScale)
            {
            }
        };

    private:
        using InstanceMap = std::map<VFS::Path::Normalized, std::vector<GroundcoverEntry>, std::less<>>;

        Resource::SceneManager* mSceneManager;
        float mDensity;
        osg::ref_ptr<osg::StateSet> mStateset;
        osg::ref_ptr<osg::Program> mProgramTemplate;
        const MWWorld::GroundcoverStore& mGroundcoverStore;

        osg::ref_ptr<osg::Node> createChunk(InstanceMap& instances, const osg::Vec2f& center);
        void collectInstances(InstanceMap& instances, float size, const osg::Vec2f& center);
    };
}
