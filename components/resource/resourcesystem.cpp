#include "resourcesystem.hpp"

#include <algorithm>

#include "animblendrulesmanager.hpp"
#include "bgsmfilemanager.hpp"
#include "imagemanager.hpp"
#include "keyframemanager.hpp"
#include "niffilemanager.hpp"
#include "scenemanager.hpp"

namespace Resource
{

    ResourceSystem::ResourceSystem(
        const VFS::Manager* vfs, double expiryDelay, const ToUTF8::StatelessUtf8Encoder* encoder)
        : mVFS(vfs)
    {
        mNifFileManager = std::make_unique<NifFileManager>(vfs, encoder);
        mBgsmFileManager = std::make_unique<BgsmFileManager>(vfs, expiryDelay);
        mImageManager = std::make_unique<ImageManager>(vfs, expiryDelay);
        mSceneManager = std::make_unique<SceneManager>(
            vfs, mImageManager.get(), mNifFileManager.get(), mBgsmFileManager.get(), expiryDelay);
        mKeyframeManager = std::make_unique<KeyframeManager>(vfs, mSceneManager.get(), expiryDelay, encoder);
        mAnimBlendRulesManager = std::make_unique<AnimBlendRulesManager>(vfs, expiryDelay);

        addResourceManager(mNifFileManager.get());
        addResourceManager(mBgsmFileManager.get());
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
        for (auto& mResourceManager : mResourceManagers)
            mResourceManager->setExpiryDelay(expiryDelay);

        // NIF files aren't needed any more once the converted objects are cached in SceneManager / BulletShapeManager,
        // so no point in using an expiry delay
        mNifFileManager->setExpiryDelay(0.0);
    }

    void ResourceSystem::updateCache(double referenceTime)
    {
        for (auto& mResourceManager : mResourceManagers)
            mResourceManager->updateCache(referenceTime);
    }

    void ResourceSystem::clearCache()
    {
        for (auto& mResourceManager : mResourceManagers)
            mResourceManager->clearCache();
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

    void ResourceSystem::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        for (auto mResourceManager : mResourceManagers)
            mResourceManager->reportStats(frameNumber, stats);
    }

    void ResourceSystem::releaseGLObjects(osg::State* state)
    {
        for (auto& mResourceManager : mResourceManagers)
            mResourceManager->releaseGLObjects(state);
    }

}
