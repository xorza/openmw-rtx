#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

#include "cachebudget.hpp"
#include "memoryarbiter.hpp"

namespace VFS
{
    class Manager;
}

namespace osg
{
    class Stats;
    class State;
}

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace Resource
{

    class SceneManager;
    class ImageManager;
    class BgsmFileManager;
    class NifFileManager;
    class SptFileManager;
    class KeyframeManager;
    class BaseResourceManager;
    class AnimBlendRulesManager;

    /// @brief Wrapper class that constructs and provides access to the most commonly used resource subsystems.
    /// @par Resource subsystems can be used with multiple OpenGL contexts, just like the OSG equivalents, but
    ///     are built around the use of a single virtual file system.
    class ResourceSystem
    {
    public:
        ResourceSystem(const ResourceSystem&) = delete;
        void operator=(const ResourceSystem&) = delete;
        explicit ResourceSystem(
            const VFS::Manager* vfs, double expiryDelay, const ToUTF8::StatelessUtf8Encoder* encoder);
        ~ResourceSystem();

        SceneManager* getSceneManager();
        ImageManager* getImageManager();
        BgsmFileManager* getBgsmFileManager();
        NifFileManager* getNifFileManager();
        SptFileManager* getSptFileManager();
        KeyframeManager* getKeyframeManager();
        AnimBlendRulesManager* getAnimBlendRulesManager();

        /// Indicates to each resource manager to clear the cache, i.e. to drop cached objects that are no longer
        /// referenced.
        /// @note May be called from any thread if you do not add or remove resource managers at that point.
        void updateCache(double referenceTime);

        /// Indicates to each resource manager to clear the entire cache.
        /// @note May be called from any thread if you do not add or remove resource managers at that point.
        void clearCache();

        /// Add this ResourceManager to be handled by the ResourceSystem.
        /// @note Does not transfer ownership.
        void addResourceManager(BaseResourceManager* resourceMgr);
        /// @note Do nothing if resourceMgr does not exist.
        /// @note Does not delete resourceMgr.
        void removeResourceManager(BaseResourceManager* resourceMgr);

        /// How long to keep objects in cache after no longer being referenced.
        void setExpiryDelay(double expiryDelay);

        /// The delay as it was last set, which the caches that keep the timer are held to: what is built from memory,
        /// the land and the terrain among it. Not a budgeted manager's, which holds none and answers nought.
        double getExpiryDelay() const { return mExpiryDelay; }

        /// What keeps the caches of what is read from files, derived from the memory the system leaves the process
        /// (`CacheBudget::derive`) as the resource system is made and at every tick of its arbiter.
        CacheBudget& getCacheBudget() { return mCacheBudget; }

        /// The one sampler of the system's memory, ticked once a second on a thread of its own.
        MemoryArbiter& getMemoryArbiter() { return mArbiter; }

        /// @note May be called from any thread.
        const VFS::Manager* getVFS() const;

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const;

        /// Call releaseGLObjects for each resource manager.
        void releaseGLObjects(osg::State* state);

    private:
        /// The arbiter's loop, until stopped.
        void arbitrate(const std::stop_token& stop);

        /// Before every manager: a budget outlives the caches held to it.
        CacheBudget mCacheBudget;
        MemoryArbiter mArbiter;

        std::unique_ptr<SceneManager> mSceneManager;
        std::unique_ptr<ImageManager> mImageManager;
        std::unique_ptr<BgsmFileManager> mBgsmFileManager;
        std::unique_ptr<NifFileManager> mNifFileManager;
        std::unique_ptr<SptFileManager> mSptFileManager;
        std::unique_ptr<KeyframeManager> mKeyframeManager;
        std::unique_ptr<AnimBlendRulesManager> mAnimBlendRulesManager;

        // Store the base classes separately to get convenient access to the common interface
        // Here users can register their own resourcemanager as well
        std::vector<BaseResourceManager*> mResourceManagers;

        const VFS::Manager* mVFS;

        double mExpiryDelay;

        /// Last, so the arbiter's thread is joined before anything it ticks goes.
        std::jthread mArbiterThread;
    };

}
