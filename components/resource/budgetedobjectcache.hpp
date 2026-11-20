#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <osg/Node>

#include "cachebudget.hpp"
#include "cachestats.hpp"
#include "objectcache.hpp"

namespace osg
{
    class Object;
    class State;
    class NodeVisitor;
}

namespace Resource
{
    /// What a load hands `BudgetedObjectCache::getOrLoad`: the value, and the bytes it holds that no other entry of
    /// a cache counts, which the budget holds the entry to.
    template <class ValueType>
    struct Loaded
    {
        ValueType mValue{};
        std::uint64_t mBytes = 0;
    };

    /// **Keeps what nothing outside it holds by a `CacheBudget`**, the residency of what is read from files
    /// (`docs/architecture.md`, "Loading"): an entry stays until the budget's caches pass their bytes and it is the
    /// least recently used, and each is loaded once however many threads ask at once. Upstream's `GenericObjectCache`
    /// keeps what the game builds from memory by time, beside it.
    template <typename KeyType, typename ValueType>
    class BudgetedObjectCache final : private BudgetedCache
    {
    public:
        /// A cache kept by `budget`, which outlives it.
        explicit BudgetedObjectCache(CacheBudget& budget)
            : mBudget(budget)
        {
            budget.join(*this);
        }

        ~BudgetedObjectCache()
        {
            mBudget.leave(*this);
            mBudget.remove(mBytes);
        }

        BudgetedObjectCache(const BudgetedObjectCache&) = delete;
        BudgetedObjectCache& operator=(const BudgetedObjectCache&) = delete;

        /// Stamps what anything holds now as used, which moves it to the front of the order eviction reads from the
        /// back; an entry goes only to make room, which the budget makes once all its caches have updated. Time keeps
        /// nothing here, so both figures a timed cache reads go unread.
        ///
        /// **In runs of `sTouchRun` entries, the lock let go between them**: a loader asking the cache waits for one
        /// run and not for the whole cache's thousands. Bounded by the entries the walk passes, which a key inserted
        /// behind it does not add to.
        void update(double /*referenceTime*/, double /*expiryDelay*/)
        {
            std::optional<KeyType> next;
            while (true)
            {
                const std::scoped_lock lock(mMutex);
                auto it = next.has_value() ? mItems.lower_bound(*next) : mItems.begin();
                for (std::size_t walked = 0; it != mItems.end() && walked < sTouchRun; ++it, ++walked)
                    if (useCount(it->second.mValue) > 1)
                        touch(*it);
                if (it == mItems.end())
                    return;
                next = it->first;
            }
        }

        /// Removes every entry, held or not, and lets go of every byte the budget counted.
        void clear()
        {
            const std::scoped_lock lock(mMutex);
            for (const auto& [key, item] : mItems)
                mGone.insert(key);
            mItems.clear();
            mNewest = nullptr;
            mOldest = nullptr;
            mBudget.remove(mBytes);
            mBytes = 0;
        }

        /// **The value `key` names, loaded once however many threads ask at once**: from the cache where it holds
        /// one; else the first thread to ask runs `load` itself, on its own stack and never queued, and every other
        /// thread that asks before it ends waits for that one load and takes its value — Go's `singleflight`, and
        /// Bevy's one handle for a path still loading. The value enters the cache once. A load that throws answers
        /// every waiter with its exception and leaves neither an entry nor a load in flight, so the next ask loads
        /// again. `load` must not ask this cache for `key`, which would wait on itself; a manager's load asks other
        /// caches only. `load` answers a `Loaded`, whose bytes the budget holds the entry to.
        template <class K, class Load>
        ValueType getOrLoad(const K& key, Load&& load)
        {
            std::promise<ValueType> promise;
            std::shared_future<ValueType> another;
            {
                const std::scoped_lock lock(mMutex);
                if (Item* const item = find(key))
                    return item->mValue;
                if (const auto found = mFlights.find(key); found != mFlights.end())
                {
                    ++mCoalesced;
                    another = found->second;
                }
                else
                    mFlights.emplace(KeyType(key), promise.get_future().share());
            }
            if (another.valid())
                return another.get();

            Loaded<ValueType> loaded;
            try
            {
                loaded = std::forward<Load>(load)();
                const std::scoped_lock lock(mMutex);
                insert(KeyType(key), loaded.mValue, loaded.mBytes);
                // Never throws: what `insert` threw left the load in flight for the catch below to end.
                mFlights.erase(mFlights.find(key));
            }
            catch (...)
            {
                {
                    const std::scoped_lock lock(mMutex);
                    mFlights.erase(mFlights.find(key));
                }
                promise.set_exception(std::current_exception());
                throw;
            }
            promise.set_value(loaded.mValue);
            // Outside the lock, since making room takes this cache's lock and others'; the entry is held here, so
            // the room made is never its own.
            mBudget.makeRoomForInsertion(loaded.mBytes);
            return std::move(loaded.mValue);
        }

        /// Removes the entry under `key`, where one stands. **Its value leaves by eviction's door**, to the list the
        /// budget's tick destroys, so the texels a renderer gives back are never freed under the cache's lock nor on
        /// its frame.
        void removeFromObjectCache(const auto& key)
        {
            const std::scoped_lock lock(mMutex);
            const auto itr = mItems.find(key);
            if (itr == mItems.end())
                return;
            mGone.insert(itr->first);
            forget(*itr);
            mEvicted.push_back(std::move(itr->second.mValue));
            mItems.erase(itr);
        }

        /// The value under `key`, stamped as used; a default-constructed one where none stands.
        ValueType getRefFromObjectCache(const auto& key)
        {
            const std::scoped_lock lock(mMutex);
            if (Item* const item = find(key))
                return item->mValue;
            return ValueType();
        }

        /// The value under `key`, stamped as used; nothing where none stands.
        std::optional<ValueType> getRefFromObjectCacheOrNone(const auto& key)
        {
            const std::scoped_lock lock(mMutex);
            if (Item* const item = find(key))
                return item->mValue;
            return std::nullopt;
        }

        /// Whether an entry stands under `key`, stamped as used where one does.
        bool checkIn(const auto& key)
        {
            const std::scoped_lock lock(mMutex);
            return find(key) != nullptr;
        }

        /// Calls `releaseGLObjects` on every value, where the value type has one (`objectcache.hpp`).
        void releaseGLObjects(osg::State* state)
            requires ReleasesGLObjects<ValueType>
        {
            const std::scoped_lock lock(mMutex);
            for (const auto& [k, v] : mItems)
                v.mValue->releaseGLObjects(state);
        }

        /// Has every node the cache holds accept `nv`, where the value type holds an OSG object (`objectcache.hpp`).
        void accept(osg::NodeVisitor& nv)
            requires HoldsOsgObject<ValueType>
        {
            const std::scoped_lock lock(mMutex);
            for (const auto& [k, v] : mItems)
                if (osg::Object* const object = v.mValue.get())
                    if (auto* const node = dynamic_cast<osg::Node*>(object))
                        node->accept(nv);
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

            /// What the entry holds that no other entry of the cache counts.
            std::uint64_t mBytes = 0;

            /// The budget's stamp of its last use, and its neighbours in the order of use.
            std::uint64_t mUsed = 0;
            Entry* mNewer = nullptr;
            Entry* mOlder = nullptr;
        };

        /// How many entries `update` stamps under one hold of the lock.
        static constexpr std::size_t sTouchRun = 256;

        std::map<KeyType, Item, std::less<>> mItems;
        mutable std::mutex mMutex;
        std::size_t mGet = 0;
        std::size_t mHit = 0;
        std::size_t mExpired = 0;
        std::size_t mLoaded = 0;
        std::size_t mCoalesced = 0;
        std::size_t mReloaded = 0;
        std::uint64_t mBytes = 0;

        CacheBudget& mBudget;

        /// The loads in flight, by key: what each one's waiters take, set once by the thread loading.
        std::map<KeyType, std::shared_future<ValueType>, std::less<>> mFlights;

        /// The order of use, newest first: what eviction reads from the back.
        Entry* mNewest = nullptr;
        Entry* mOldest = nullptr;

        /// Every key an entry left under, so a load of it again is counted as one (`CacheStats::mReloaded`): a run's
        /// measure of how much the budget made it read twice. Bounded by the keys the content can name, one entry
        /// each.
        std::set<KeyType, std::less<>> mGone;

        /// What evictions let go of since the last `destroyEvicted`, which the budget's arbiter calls once a second:
        /// bounded by what the inserting threads put in past the budget in that second, each insertion evicting as
        /// many bytes as it added.
        std::vector<ValueType> mEvicted;

        /// Puts `object` under `key`, counted as a load and by the budget, under the lock. An entry it replaces is
        /// let go of first.
        template <class K>
        void insert(K&& key, ValueType object, std::uint64_t bytes)
        {
            ++mLoaded;
            if (const auto gone = mGone.find(key); gone != mGone.end())
            {
                ++mReloaded;
                mGone.erase(gone);
            }
            auto it = mItems.find(key);
            if (it == mItems.end())
                it = mItems.emplace_hint(it, std::forward<K>(key), Item{ .mValue = std::move(object) });
            else
            {
                forget(*it);
                it->second = Item{ .mValue = std::move(object) };
            }
            it->second.mBytes = bytes;
            mBytes += bytes;
            mBudget.add(bytes);
            touch(*it);
        }

        Item* find(const auto& key)
        {
            ++mGet;
            const auto it = mItems.find(key);
            if (it == mItems.end())
                return nullptr;
            ++mHit;
            touch(*it);
            return &it->second;
        }

        /// Stamps `entry` as used now and moves it to the front, under the lock.
        void touch(Entry& entry)
        {
            unlink(entry);
            Item& item = entry.second;
            item.mUsed = mBudget.stamp();
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
            mBudget.remove(entry.second.mBytes);
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
            const std::scoped_lock lock(mMutex);
            if (mOldest == nullptr || mOldest->second.mUsed != used || useCount(mOldest->second.mValue) > 1)
                return false;
            Entry& entry = *mOldest;
            mEvicted.push_back(std::move(entry.second.mValue));
            ++mExpired;
            mGone.insert(entry.first);
            forget(entry);
            mItems.erase(mItems.find(entry.first));
            return true;
        }

        void destroyEvicted() final
        {
            std::vector<ValueType> evicted;
            {
                const std::scoped_lock lock(mMutex);
                evicted.swap(mEvicted);
            }
            // Here, outside the lock, so no value is destroyed under it.
            evicted.clear();
        }
    };
}
