#include "resourcesystem.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>

#include <components/platform/heap.hpp>
#include <components/platform/memory.hpp>
#include <components/platform/thread.hpp>

#include "animblendrulesmanager.hpp"
#include "bgsmfilemanager.hpp"
#include "imagemanager.hpp"
#include "keyframemanager.hpp"
#include "niffilemanager.hpp"
#include "scenemanager.hpp"
#include "sptfilemanager.hpp"

namespace Resource
{

    ResourceSystem::ResourceSystem(
        const VFS::Manager* vfs, double expiryDelay, const ToUTF8::StatelessUtf8Encoder* encoder)
        : mCacheBudget(CacheBudget::derive(Platform::availableMemory(), 0, 0))
        , mArbiter(mCacheBudget)
        , mVFS(vfs)
        , mExpiryDelay(expiryDelay)
    {
        mNifFileManager = std::make_unique<NifFileManager>(vfs, mCacheBudget, encoder);
        mBgsmFileManager = std::make_unique<BgsmFileManager>(vfs, expiryDelay);
        mSptFileManager = std::make_unique<SptFileManager>(vfs, expiryDelay);
        mImageManager = std::make_unique<ImageManager>(vfs, mCacheBudget);
        mSceneManager = std::make_unique<SceneManager>(vfs, mImageManager.get(), mNifFileManager.get(),
            mBgsmFileManager.get(), mSptFileManager.get(), mCacheBudget);
        mKeyframeManager = std::make_unique<KeyframeManager>(vfs, mSceneManager.get(), mCacheBudget, encoder);
        mAnimBlendRulesManager = std::make_unique<AnimBlendRulesManager>(vfs, expiryDelay);

        addResourceManager(mNifFileManager.get());
        addResourceManager(mBgsmFileManager.get());
        addResourceManager(mSptFileManager.get());
        addResourceManager(mKeyframeManager.get());
        // note, scene references images so add images afterwards for correct implementation of updateCache()
        addResourceManager(mSceneManager.get());
        addResourceManager(mImageManager.get());
        addResourceManager(mAnimBlendRulesManager.get());

        mArbiterThread
            = Platform::startThread("memory arbiter", [this](const std::stop_token& stop) { arbitrate(stop); });
    }

    ResourceSystem::~ResourceSystem()
    {
        // this has to be defined in the .cpp file as we can't delete incomplete types

        mResourceManagers.clear();
    }

    void ResourceSystem::arbitrate(const std::stop_token& stop)
    {
        // **The system read once a tick, after the caches' evictions are destroyed** (`MemoryArbiter::tick`), off every
        // frame and every loader's queue.
        std::mutex mutex;
        std::condition_variable_any woken;
        while (!stop.stop_requested())
        {
            mArbiter.tick([] {
                const std::optional<Platform::HeapHeld> heap = Platform::heapHeld();
                return SystemReading{
                    .mAvailable = Platform::availableMemory(),
                    .mHeapFree = heap.has_value() && heap->mTaken > heap->mInUse ? heap->mTaken - heap->mInUse : 0,
                    .mAt = std::chrono::steady_clock::now(),
                };
            });
            std::unique_lock lock(mutex);
            woken.wait_for(lock, stop, MemoryArbiter::sPeriod, [] { return false; });
        }
    }

    SceneManager* ResourceSystem::getSceneManager()
    {
        return mSceneManager.get();
    }

    ImageManager* ResourceSystem::getImageManager()
    {
        return mImageManager.get();
    }

    BgsmFileManager* ResourceSystem::getBgsmFileManager()
    {
        return mBgsmFileManager.get();
    }

    NifFileManager* ResourceSystem::getNifFileManager()
    {
        return mNifFileManager.get();
    }

    SptFileManager* ResourceSystem::getSptFileManager()
    {
        return mSptFileManager.get();
    }

    KeyframeManager* ResourceSystem::getKeyframeManager()
    {
        return mKeyframeManager.get();
    }

    AnimBlendRulesManager* ResourceSystem::getAnimBlendRulesManager()
    {
        return mAnimBlendRulesManager.get();
    }

    void ResourceSystem::setExpiryDelay(double expiryDelay)
    {
        mExpiryDelay = expiryDelay;
        for (auto& resourceManager : mResourceManagers)
            resourceManager->setExpiryDelay(expiryDelay);
    }

    void ResourceSystem::updateCache(double referenceTime)
    {
        for (auto& resourceManager : mResourceManagers)
            resourceManager->updateCache(referenceTime);
    }

    void ResourceSystem::clearCache()
    {
        for (auto& resourceManager : mResourceManagers)
            resourceManager->clearCache();
    }

    void ResourceSystem::addResourceManager(BaseResourceManager* resourceMgr)
    {
        mResourceManagers.push_back(resourceMgr);
    }

    void ResourceSystem::removeResourceManager(BaseResourceManager* resourceMgr)
    {
        auto found = std::find(mResourceManagers.begin(), mResourceManagers.end(), resourceMgr);
        if (found != mResourceManagers.end())
            mResourceManagers.erase(found);
    }

    const VFS::Manager* ResourceSystem::getVFS() const
    {
        return mVFS;
    }

    void ResourceSystem::reportStats(std::uint32_t frameNumber, osg::Stats* stats) const
    {
        for (auto resourceManager : mResourceManagers)
            resourceManager->reportStats(frameNumber, stats);
    }

    void ResourceSystem::releaseGLObjects(osg::State* state)
    {
        for (auto& resourceManager : mResourceManagers)
            resourceManager->releaseGLObjects(state);
    }

}
