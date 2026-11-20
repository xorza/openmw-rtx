#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <osg/Node>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

#include "budgetedobjectcache.hpp"
#include "cachebudget.hpp"
#include "objectcache.hpp"

namespace VFS
{
    class Manager;
}

namespace osg
{
    class Stats;
    class State;
}

namespace Resource
{

    class BaseResourceManager
    {
    public:
        virtual ~BaseResourceManager() = default;
        virtual void updateCache(double referenceTime) = 0;
        virtual void clearCache() = 0;
        virtual void setExpiryDelay(double expiryDelay) = 0;
        virtual void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const = 0;
        virtual void releaseGLObjects(osg::State* state) = 0;
    };

    /// @brief Base class for managers that require a virtual file system and object cache.
    /// @par This base class implements clearing of the cache, but populating it and what it's used for is up to the
    /// individual sub classes.
    ///
    /// @tparam Cache what keeps the values: upstream's `GenericObjectCache` by time, or the fork's
    ///         `BudgetedObjectCache` by a `CacheBudget`, for what is read from files.
    template <class KeyType, class ValueType, class Cache = GenericObjectCache<KeyType, ValueType>>
    class GenericResourceManager : public BaseResourceManager
    {
    public:
        using CacheType = Cache;

        explicit GenericResourceManager(const VFS::Manager* vfs, double expiryDelay)
            requires std::is_default_constructible_v<Cache>
            : mVFS(vfs)
            , mCache(std::make_unique<CacheType>())
            , mExpiryDelay(expiryDelay)
        {
        }

        /// A manager of what is read from files, whose cache `budget` keeps rather than time.
        explicit GenericResourceManager(const VFS::Manager* vfs, CacheBudget& budget)
            requires std::is_constructible_v<Cache, CacheBudget&>
            : mVFS(vfs)
            , mCache(std::make_unique<CacheType>(budget))
            , mExpiryDelay(0.0)
        {
        }

        ~GenericResourceManager() override = default;

        /// Clear cache entries that have not been referenced for longer than expiryDelay.
        void updateCache(double referenceTime) override { mCache->update(referenceTime, mExpiryDelay); }

        /// Clear all cache entries.
        void clearCache() override { mCache->clear(); }

        /// Drops the entry `key` names, where the cache holds one: a holder keeps the object, and the
        /// next ask loads it again.
        void removeFromCache(const auto& key) { mCache->removeFromObjectCache(key); }

        /// How long to keep objects in cache after no longer being referenced.
        void setExpiryDelay(double expiryDelay) final { mExpiryDelay = expiryDelay; }
        double getExpiryDelay() const { return mExpiryDelay; }

        const VFS::Manager* getVFS() const { return mVFS; }

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override {}

        void releaseGLObjects(osg::State* state) override
        {
            // Only meaningful for caches whose values are GL-backed; see objectcache.hpp.
            if constexpr (requires { mCache->releaseGLObjects(state); })
                mCache->releaseGLObjects(state);
        }

    protected:
        const VFS::Manager* mVFS;
        std::unique_ptr<CacheType> mCache;
        double mExpiryDelay;
    };

    /// Caching a scene graph is the common case, so it gets a name rather than the
    /// same template arguments at every use.
    template <class KeyType>
    using NodeResourceManager = GenericResourceManager<KeyType, osg::ref_ptr<osg::Node>>;

    /// A manager of nodes read from files, kept by a budget.
    template <class KeyType>
    using BudgetedNodeResourceManager = GenericResourceManager<KeyType, osg::ref_ptr<osg::Node>,
        BudgetedObjectCache<KeyType, osg::ref_ptr<osg::Node>>>;

    template <class ValueType, class Cache = GenericObjectCache<std::string, ValueType>>
    class ResourceManager : public GenericResourceManager<std::string, ValueType, Cache>
    {
    public:
        using GenericResourceManager<std::string, ValueType, Cache>::GenericResourceManager;

    protected:
        using GenericResourceManager<std::string, ValueType, Cache>::mCache;
        using GenericResourceManager<std::string, ValueType, Cache>::mVFS;
    };

    /// A manager of what is read from files, by name, kept by a budget.
    template <class ValueType>
    using BudgetedResourceManager = ResourceManager<ValueType, BudgetedObjectCache<std::string, ValueType>>;

}
