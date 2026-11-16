// Resource ObjectCache for OpenMW, forked from osgDB ObjectCache by Robert Osfield, see copyright notice below.
// Changes:
// - removeExpiredObjectsInCache no longer keeps a lock while the unref happens.
// - template allows customized KeyType.
// - objects with uninitialized time stamp are not removed.

/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
 */

#pragma once

#include "cachebudget.hpp"
#include "cachestats.hpp"

#include <osg/Node>
#include <osg/ref_ptr>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace osg
{
    class Object;
    class State;
    class NodeVisitor;
    class Stats;
}

namespace Resource
{
    // How the cache asks "is anything outside the cache still holding this?".
    //
    // Free overloads rather than a member, so the cache never has to name a base
    // class its values are expected to derive from. This is the extension point: a
    // value type the cache has not seen before is supported by adding an overload
    // here, not by changing the cache or the type. A second rendering backend would
    // add one for its own pointer, since scene-graph objects must be owned by the
    // pointer their library refs them with — a graph holds references to its own
    // children, so a second refcount over the same object would be a bug, not a
    // design choice.
    template <class T>
    long useCount(const std::shared_ptr<T>& value)
    {
        return value.use_count();
    }

    template <class T>
    long useCount(const osg::ref_ptr<T>& value)
    {
        // nullptr is a storable value, so this has to answer for it; shared_ptr's
        // use_count() already returns 0 for an empty pointer.
        return value == nullptr ? 0 : value->referenceCount();
    }

    // The two things a value type may or may not be able to do. Named, because an
    // inline requires-expression on the member declaration formats unreadably.
    template <class T>
    concept ReleasesGLObjects = requires(T value, osg::State* state) { value->releaseGLObjects(state); };

    template <class T>
    concept HoldsOsgObject = std::is_convertible_v<decltype(std::declval<T>().get()), osg::Object*>;

    /// What a load hands `GenericObjectCache::getOrLoad`: the value, and the bytes it holds that no other entry of
    /// a cache counts, which a cache held to a budget is kept by.
    template <class ValueType>
    struct Loaded
    {
        ValueType mValue{};
        std::uint64_t mBytes = 0;
    };

    /// Keeps what nothing outside it holds by one of two residencies: by time, where `update` lets go of an
    /// entry nothing held for its delay, the residency of what the game builds from memory; or by a
    /// `CacheBudget`, where an entry stays until the budget's caches pass their bytes and it is the least
    /// recently used, the residency of what is read from files (`docs/architecture.md`, "Loading").
    template <typename KeyType, typename ValueType>
    class GenericObjectCache final : private BudgetedCache
    {
    public:
        /// A cache kept by time.
        GenericObjectCache() = default;

        /// A cache kept by `budget`, which outlives it.
        explicit GenericObjectCache(CacheBudget& budget)
            : mBudget(&budget)
        {
            budget.join(*this);
        }

        ~GenericObjectCache()
        {
            if (mBudget == nullptr)
                return;
            mBudget->leave(*this);
            mBudget->remove(mBytes);
        }

        GenericObjectCache(const GenericObjectCache&) = delete;
        GenericObjectCache& operator=(const GenericObjectCache&) = delete;

        /*
         * @brief Updates usage timestamps and removes expired items
         *
         * Updates the lastUsage timestamp of cached non-nullptr items that have external references.
         * Initializes lastUsage timestamp for new items.
         * Removes items that haven't been referenced for longer than expiryDelay.
         *
         * \note
         * Last usage might be updated from other places so nullptr items
         * that are not referenced elsewhere are not always removed.
         *
         * @param referenceTime the timestamp indicating when the item was most recently used
         * @param expiryDelay the delay after which the cache entry for an item expires
         */
        void update(double referenceTime, double expiryDelay)
        {
            // Kept by a budget, an entry goes only to make room, which the budget makes once all its caches have
            // updated; what anything holds now is in use, and moves to the front of the order eviction reads from
            // the back.
            if (mBudget != nullptr)
            {
                const std::scoped_lock lock(mMutex);
                for (auto& entry : mItems)
                    if (useCount(entry.second.mValue) > 1)
                        touch(entry);
                return;
            }

            std::vector<ValueType> objectsToRemove;
            {
                const double expiryTime = referenceTime - expiryDelay;
                std::scoped_lock lock(mMutex);

                std::erase_if(mItems, [&](auto& v) {
                    Item& item = v.second;

                    // update last usage timestamp if item is being referenced externally
                    // or initialize if not set
                    if (useCount(item.mValue) > 1 || item.mLastUsage == 0)
                        item.mLastUsage = referenceTime;

                    // skip items that have been accessed since expiryTime
                    if (item.mLastUsage > expiryTime)
                        return false;

                    ++mExpired;
                    mGone.insert(v.first);

                    // just mark for removal here so objects can be removed in bulk outside the lock
                    if (item.mValue != nullptr)
                        objectsToRemove.push_back(std::move(item.mValue));

                    return true;
                });
            }
            // remove expired items from cache
            objectsToRemove.clear();
        }

        /** Remove all objects in the cache regardless of having external references or expiry times.*/
        void clear()
        {
            std::scoped_lock lock(mMutex);
            for (const auto& [key, item] : mItems)
                mGone.insert(key);
            mItems.clear();
            mNewest = nullptr;
            mOldest = nullptr;
            if (mBudget != nullptr)
                mBudget->remove(mBytes);
            mBytes = 0;
        }

        /** Add a key,object,timestamp triple to the Registry::ObjectCache.*/
        template <class K>
        void addEntryToObjectCache(K&& key, ValueType object, double timestamp = 0.0)
        {
            assert(mBudget == nullptr && "a cache kept by a budget takes its entries with their bytes, by getOrLoad");
            std::scoped_lock lock(mMutex);
            insert(std::forward<K>(key), std::move(object), timestamp, 0);
        }

        /// **The value `key` names, loaded once however many threads ask at once**: from the cache where it holds
        /// one; else the first thread to ask runs `load` itself, on its own stack and never queued, and every other
        /// thread that asks before it ends waits for that one load and takes its value — Go's `singleflight`, and
        /// Bevy's one handle for a path still loading. The value enters the cache once. A load that throws answers
        /// every waiter with its exception and leaves neither an entry nor a load in flight, so the next ask loads
        /// again. `load` must not ask this cache for `key`, which would wait on itself; a manager's load asks other
        /// caches only. `load` answers a `Loaded`, whose bytes a budget holds the entry to.
        template <class K, class Load>
        ValueType getOrLoad(const K& key, Load&& load)
        {
            std::shared_ptr<Flight> flight;
            bool mine = false;
            {
                const std::scoped_lock lock(mMutex);
                if (Item* const item = find(key))
                    return item->mValue;
                auto found = mFlights.find(key);
                if (found == mFlights.end())
                {
                    found = mFlights.emplace(KeyType(key), std::make_shared<Flight>()).first;
                    mine = true;
                }
                else
                    ++mCoalesced;
                flight = found->second;
            }

            if (!mine)
                return flight->mResult.get();

            Loaded<ValueType> loaded;
            try
            {
                loaded = std::forward<Load>(load)();
                const std::scoped_lock lock(mMutex);
                insert(KeyType(key), loaded.mValue, 0.0, loaded.mBytes);
                // Never throws: what `insert` threw left the load in flight for the catch below to end.
                mFlights.erase(mFlights.find(key));
            }
            catch (...)
            {
                {
                    const std::scoped_lock lock(mMutex);
                    mFlights.erase(mFlights.find(key));
                }
                flight->mPromise.set_exception(std::current_exception());
                throw;
            }
            flight->mPromise.set_value(loaded.mValue);
            // Outside the lock, since making room takes this cache's lock and others'; the entry is held here, so
            // the room made is never its own.
            if (mBudget != nullptr)
                mBudget->makeRoomForInsertion();
            return std::move(loaded.mValue);
        }

        /** Remove Object from cache.*/
        void removeFromObjectCache(const auto& key)
        {
            std::scoped_lock lock(mMutex);
            const auto itr = mItems.find(key);
            if (itr != mItems.end())
            {
                mGone.insert(itr->first);
                forget(*itr);
                mItems.erase(itr);
            }
        }

        /** Get a value from the object cache; a default-constructed one if absent. */
        ValueType getRefFromObjectCache(const auto& key)
        {
            std::scoped_lock lock(mMutex);
            if (Item* const item = find(key))
                return item->mValue;
            return ValueType();
        }

        std::optional<ValueType> getRefFromObjectCacheOrNone(const auto& key)
        {
            const std::scoped_lock lock(mMutex);
            if (Item* const item = find(key))
                return item->mValue;
            return std::nullopt;
        }

        /** Check if an object is in the cache, and if it is, update its usage time stamp. */
        bool checkInObjectCache(const auto& key, double timeStamp)
        {
            std::scoped_lock lock(mMutex);
            if (Item* const item = find(key))
            {
                item->mLastUsage = timeStamp;
                return true;
            }
            return false;
        }

        /** call releaseGLObjects on all objects attached to the object cache.
            Constrained, so instantiating the cache with a value type that has no such
            method is not an error — it simply does not offer this. */
        void releaseGLObjects(osg::State* state)
            requires ReleasesGLObjects<ValueType>
        {
            std::scoped_lock lock(mMutex);
            for (const auto& [k, v] : mItems)
                v.mValue->releaseGLObjects(state);
        }

        /** call node->accept(nv); for all nodes in the objectCache. Constrained for
            the same reason as releaseGLObjects. */
        void accept(osg::NodeVisitor& nv)
            requires HoldsOsgObject<ValueType>
        {
            std::scoped_lock lock(mMutex);
            for (const auto& [k, v] : mItems)
                if (osg::Object* const object = v.mValue.get())
                    if (auto* const node = dynamic_cast<osg::Node*>(object))
                        node->accept(nv);
        }

        /** call operator()(KeyType, osg::Object*) for each object in the cache. */
        template <class Functor>
        void call(Functor&& f)
        {
            std::scoped_lock lock(mMutex);
            for (const auto& [k, v] : mItems)
                f(k, v.mValue.get());
        }

        template <class K>
        std::optional<std::pair<KeyType, ValueType>> lowerBound(K&& key)
        {
            const std::scoped_lock lock(mMutex);
            const auto it = mItems.lower_bound(std::forward<K>(key));
            if (it == mItems.end())
                return std::nullopt;
            return std::pair(it->first, it->second.mValue);
        }

        CacheStats getStats() const
        {
            const std::scoped_lock lock(mMutex);
            return CacheStats{
                .mSize = mItems.size(),
                .mGet = mGet,
                .mHit = mHit,
                .mExpired = mExpired,
                .mLoaded = mLoaded,
                .mCoalesced = mCoalesced,
                .mReloaded = mReloaded,
                .mBytes = mBytes,
            };
        }

    private:
        struct Item;
        using Entry = std::pair<const KeyType, Item>;

        struct Item
        {
            ValueType mValue;
            double mLastUsage;

            /// What the entry holds that no other entry of the cache counts.
            std::uint64_t mBytes = 0;

            /// Kept by a budget: the budget's stamp of its last use, and its neighbours in the order of use.
            std::uint64_t mUsed = 0;
            Entry* mNewer = nullptr;
            Entry* mOlder = nullptr;
        };

        /// One load in flight: what its waiters take, set once by the thread loading.
        struct Flight
        {
            std::promise<ValueType> mPromise;
            std::shared_future<ValueType> mResult = mPromise.get_future().share();
        };

        std::map<KeyType, Item, std::less<>> mItems;
        mutable std::mutex mMutex;
        std::size_t mGet = 0;
        std::size_t mHit = 0;
        std::size_t mExpired = 0;
        std::size_t mLoaded = 0;
        std::size_t mCoalesced = 0;
        std::size_t mReloaded = 0;
        std::uint64_t mBytes = 0;

        /// What keeps an entry nothing holds, where a budget does rather than time.
        CacheBudget* mBudget = nullptr;

        /// Kept by a budget: the entries in the order of their last use, which only the cache's lock reaches.
        Entry* mNewest = nullptr;
        Entry* mOldest = nullptr;

        /// The loads in flight, by key: at most one a key, and gone the moment its load ends.
        std::map<KeyType, std::shared_ptr<Flight>, std::less<>> mFlights;

        /// Every key the cache let go of, whichever way, so a load of one again is counted: bounded by the keys the
        /// content can name, one entry each.
        std::set<KeyType, std::less<>> mGone;

        /// Puts `object` under `key`, counted as a load and by the budget, under the lock. An entry it replaces is
        /// let go of first.
        template <class K>
        void insert(K&& key, ValueType object, double timestamp, std::uint64_t bytes)
        {
            ++mLoaded;
            if (const auto gone = mGone.find(key); gone != mGone.end())
            {
                ++mReloaded;
                mGone.erase(gone);
            }
            auto it = mItems.find(key);
            if (it == mItems.end())
                it = mItems.emplace_hint(it, std::forward<K>(key), Item{ std::move(object), timestamp });
            else
            {
                forget(*it);
                it->second = Item{ std::move(object), timestamp };
            }
            it->second.mBytes = bytes;
            mBytes += bytes;
            if (mBudget != nullptr)
            {
                mBudget->add(bytes);
                touch(*it);
            }
        }

        Item* find(const auto& key)
        {
            ++mGet;
            const auto it = mItems.find(key);
            if (it == mItems.end())
                return nullptr;
            ++mHit;
            if (mBudget != nullptr)
                touch(*it);
            return &it->second;
        }

        /// Stamps `entry` as used now and moves it to the front, under the lock.
        void touch(Entry& entry)
        {
            unlink(entry);
            Item& item = entry.second;
            item.mUsed = mBudget->stamp();
            item.mOlder = mNewest;
            if (mNewest != nullptr)
                mNewest->second.mNewer = &entry;
            mNewest = &entry;
            if (mOldest == nullptr)
                mOldest = &entry;
        }

        /// Takes `entry` out of the order of use, which it may not be in.
        void unlink(Entry& entry)
        {
            Item& item = entry.second;
            if (item.mNewer != nullptr)
                item.mNewer->second.mOlder = item.mOlder;
            else if (mNewest == &entry)
                mNewest = item.mOlder;
            if (item.mOlder != nullptr)
                item.mOlder->second.mNewer = item.mNewer;
            else if (mOldest == &entry)
                mOldest = item.mNewer;
            item.mNewer = nullptr;
            item.mOlder = nullptr;
        }

        /// Lets go of what `entry` counts for, under the lock, before it is erased or replaced.
        void forget(Entry& entry)
        {
            unlink(entry);
            mBytes -= entry.second.mBytes;
            if (mBudget != nullptr)
                mBudget->remove(entry.second.mBytes);
        }

        std::optional<std::uint64_t> oldestUnheld() final
        {
            const std::scoped_lock lock(mMutex);
            // Bounded by the entries: each held one passed goes to the front, so a second pass would meet it again.
            for (std::size_t passed = 0; mOldest != nullptr && passed < mItems.size(); ++passed)
            {
                if (useCount(mOldest->second.mValue) <= 1)
                    return mOldest->second.mUsed;
                touch(*mOldest);
            }
            return std::nullopt;
        }

        bool evictOldest(std::uint64_t used) final
        {
            ValueType value;
            {
                const std::scoped_lock lock(mMutex);
                if (mOldest == nullptr || mOldest->second.mUsed != used || useCount(mOldest->second.mValue) > 1)
                    return false;
                Entry& entry = *mOldest;
                value = std::move(entry.second.mValue);
                ++mExpired;
                mGone.insert(entry.first);
                forget(entry);
                mItems.erase(mItems.find(entry.first));
            }
            // `value` goes here, outside the lock, as `update` lets its values go.
            return true;
        }
    };
}
