#pragma once

#include <cstdint>
#include <mutex>

#include <components/esm3/refnum.hpp>
#include <components/resource/resourcemanager.hpp>
#include <components/terrain/quadtreeworld.hpp>

namespace Resource
{
    class SceneManager;
}

namespace MWRender
{

    typedef std::tuple<osg::Vec2f, float, bool> ChunkId; // Center, Size, ActiveGrid

    class ObjectPaging : public Resource::NodeResourceManager<ChunkId>, public Terrain::QuadTreeWorld::ChunkManager
    {
    public:
        ObjectPaging(Resource::SceneManager* sceneManager, ESM::RefId worldspace);
        ~ObjectPaging() override = default;

        osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f& center, std::uint8_t lod, std::uint32_t lodFlags,
            bool activeGrid, const osg::Vec3f& viewPoint, bool compile) override;

        osg::ref_ptr<osg::Node> createChunk(float size, const osg::Vec2f& center, bool activeGrid,
            const osg::Vec3f& viewPoint, bool compile, std::uint8_t lod);

        std::uint32_t getNodeMask() override;

        /// @return true if view needs rebuild
        bool enableObject(
            std::int32_t type, ESM::RefNum refnum, const osg::Vec3f& pos, const osg::Vec2i& cell, bool enabled);

        /// @return true if view needs rebuild
        bool blacklistObject(std::int32_t type, ESM::RefNum refnum, const osg::Vec3f& pos, const osg::Vec2i& cell);

        void clear();

        /// Must be called after clear() before rendering starts.
        /// @return true if view needs rebuild
        bool unlockCache();

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override;

        void getPagedRefnums(const osg::Vec4i& activeGrid, std::vector<ESM::RefNum>& out);

    private:
        Resource::SceneManager* mSceneManager;
        bool mActiveGrid;
        bool mDebugBatches;
        float mMergeFactor;
        float mMinSize;
        float mMinSizeMergeFactor;
        float mMinSizeCostMultiplier;

        std::mutex mRefTrackerMutex;
        struct RefTracker
        {
            std::set<ESM::RefNum> mDisabled;
            std::set<ESM::RefNum> mBlacklist;
            bool operator==(const RefTracker& other) const
            {
                return mDisabled == other.mDisabled && mBlacklist == other.mBlacklist;
            }
        };
        RefTracker mRefTracker;
        RefTracker mRefTrackerNew;
        bool mRefTrackerLocked;

        const RefTracker& getRefTracker() const { return mRefTracker; }
        RefTracker& getWritableRefTracker() { return mRefTrackerLocked ? mRefTrackerNew : mRefTracker; }

        std::mutex mSizeCacheMutex;
        typedef std::map<ESM::RefNum, float> SizeCache;
        SizeCache mSizeCache;

        std::mutex mLODNameCacheMutex;
        typedef std::pair<std::string, std::uint8_t> LODNameCacheKey; // Key: mesh name, lod level
        using LODNameCache = std::map<LODNameCacheKey, VFS::Path::Normalized>; // Cache: key, mesh name to use
        LODNameCache mLODNameCache;
    };

    struct RefnumMarker
    {
        ESM::RefNum mRefnum;
        std::uint32_t mNumVertices = 0;
    };
}
