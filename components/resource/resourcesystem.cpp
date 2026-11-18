#include "resourcesystem.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include <components/platform/heap.hpp>
#include <components/platform/memory.hpp>

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
        , mVFS(vfs)
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
    }

    ResourceSystem::~ResourceSystem()
    {
        // this has to be defined in the .cpp file as we can't delete incomplete types

        mResourceManagers.clear();
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
        for (auto& resourceManager : mResourceManagers)
            resourceManager->setExpiryDelay(expiryDelay);
    }

    void ResourceSystem::updateCache(double referenceTime)
    {
        for (auto& resourceManager : mResourceManagers)
            resourceManager->updateCache(referenceTime);
        // After the caches stamped what is in use: one read of the system's figure and of the heap's, and one look for
        // room, off the frame, on the preloader's worker once a second and on the scene's thread as a cell changes.
        const std::optional<Platform::HeapHeld> heap = Platform::heapHeld();
        const std::uint64_t heapFree
            = heap.has_value() && heap->mTaken > heap->mInUse ? heap->mTaken - heap->mInUse : 0;
        mCacheBudget.setBytes(CacheBudget::derive(Platform::availableMemory(), mCacheBudget.getHeld(), heapFree));
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
