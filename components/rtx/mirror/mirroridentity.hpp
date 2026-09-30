#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/ref_ptr>

#include <components/rtx/common/runs.hpp>

#include "mirrorpass.hpp"

namespace Rtx
{
    /// Hashes and compares an owning key by the address it holds. A map keyed on a raw `osg`
    /// pointer can be fooled — the engine frees a body part and the allocator puts the replacement
    /// exactly where it was — and a `ref_ptr` key makes the address true; transparent, so a lookup
    /// from a raw pointer stays out of the reference count.
    template <class T>
    struct ByAddress
    {
        using is_transparent = void;

        std::size_t operator()(const osg::ref_ptr<T>& value) const { return std::hash<const T*>{}(value.get()); }
        std::size_t operator()(const T* value) const { return std::hash<const T*>{}(value); }

        bool operator()(const osg::ref_ptr<T>& left, const osg::ref_ptr<T>& right) const
        {
            return left.get() == right.get();
        }
        bool operator()(const osg::ref_ptr<T>& left, const T* right) const { return left.get() == right; }
        bool operator()(const T* left, const osg::ref_ptr<T>& right) const { return left == right.get(); }
    };

    /// What `Kept` counts of an entry: when it was last met, and what holds it. The epoch is what a
    /// sweep runs on; the holds are the other keeper, for the rows a residency stands under the
    /// same entries the walk would find a clone's mesh under. Every entry a map keeps carries one
    /// as `mReach`, beside what it names.
    struct Reach
    {
        std::uint64_t mEpoch = 0;
        std::uint32_t mHolds = 0;
    };

    /// An entry that names a row by its index.
    struct Known
    {
        Index mIndex = sNoIndex;
        Reach mReach;
    };

    /// A map of what the mirror knows, and how much of it the walk in progress has reached. The
    /// count is what lets a sweep be skipped rather than run over tens of thousands of entries to
    /// find nothing: a world that stands still reaches every entry it holds. A held entry counts as
    /// reached without being stamped, so the two counts together are the size exactly when every
    /// unheld entry was met — `whole`. The count belongs to one epoch, because a table is not
    /// always retired. Every write goes through this class, or a count kept beside the map is free
    /// to fall behind it.
    ///
    /// **An entry is good until the next insert into the same map.** Every map here is an
    /// open-addressing table, whose entries stand in its own array, so a lookup at every node and
    /// drawable of a walk is a probe of that array rather than a cache miss for each link of a
    /// bucket's list. A table grown past its room moves every entry: nothing keeps a reference into
    /// one across an insert, and what must outlive one keeps the key and finds the entry again.
    template <class Map>
    class Kept
    {
    public:
        using Entry = typename Map::iterator;

        /// What an entry holds: a `Known`, or a type of the map's own that carries a `Reach`.
        using Value = typename Map::mapped_type;

        /// What `reach` found: the entry, stamped, and whether the call is what put it there.
        struct Arrival
        {
            Entry mEntry;
            bool mArrived;
        };

        /// @param pass the walk in progress, borrowed: the epoch it stamps with is the mirror's own
        ///        rather than a copy free to fall behind it.
        explicit Kept(const MirrorPass& pass)
            : mPass(pass)
        {
        }

        Entry end() { return mKnown.end(); }

        /// Room for `count` entries before the table rehashes. A rehash on the frame a cell arrives
        /// is what this is called once to prevent.
        void reserve(std::size_t count) { mKnown.reserve(count); }

        /// The entry for `key`, or `end()`. Unstamped: `stamp` is what says the walk met it.
        template <class Key>
        Entry find(const Key& key)
        {
            return mKnown.find(key);
        }

        /// Records that the walk in progress met `entry`. An entry two walks of one epoch both
        /// reach counts once; a held entry is stamped and not counted.
        void stamp(Entry entry)
        {
            Value& held = entry->second;
            freshen();
            mReached += held.mReach.mHolds == 0 && held.mReach.mEpoch != mPass.mEpoch ? 1 : 0;
            held.mReach.mEpoch = mPass.mEpoch;
        }

        /// Takes one hold on `entry`, which keeps it — and the row it names — through every sweep
        /// until the hold is given back.
        void hold(Entry entry)
        {
            freshen();

            Value& held = entry->second;
            if (held.mReach.mHolds++ != 0)
                return;

            ++mHeld;
            mReached -= held.mReach.mEpoch == mPass.mEpoch ? 1 : 0;
        }

        /// Gives one hold back. An entry no hold and no stamp keeps is the next sweep's, and the
        /// sweep is owed for it whatever else the walk reached.
        void drop(Entry entry)
        {
            freshen();

            Value& held = entry->second;
            assert(held.mReach.mHolds > 0 && "an entry given back more often than it was held");
            if (--held.mReach.mHolds != 0)
                return;

            --mHeld;
            if (held.mReach.mEpoch == mPass.mEpoch)
                ++mReached;
            else
                mAbandoned = true;
        }

        /// Adds what the walk has just resolved, stamped, and hands the entry back. `key` must not
        /// already be held.
        template <class Key, class Held>
        Entry add(const Key& key, Held held)
        {
            freshen();
            held.mReach.mEpoch = mPass.mEpoch;
            held.mReach.mHolds = 0;

            const auto [entry, arrived] = mKnown.emplace(key, std::move(held));
            assert(arrived && "an identity the map already held, added again");

            ++mReached;
            return entry;
        }

        /// The entry for `key`, stamped, made where the map holds none. A made entry arrives with
        /// its fields default and the caller fills them.
        template <class Key>
        Arrival reach(const Key& key)
        {
            freshen();

            const auto [entry, arrived] = mKnown.try_emplace(key);
            Value& held = entry->second;
            mReached += held.mReach.mHolds == 0 && (arrived || held.mReach.mEpoch != mPass.mEpoch) ? 1 : 0;
            held.mReach.mEpoch = mPass.mEpoch;

            return Arrival{ .mEntry = entry, .mArrived = arrived };
        }

        /// Lets go of `entry` in the middle of a walk, where what it held turned out to describe
        /// something else. The frame then owes a sweep however much of the map it went on to reach,
        /// which is why the count alone cannot say a map is whole.
        void abandon(Entry entry)
        {
            freshen();

            // An entry something holds is not the walk's to let go of: the holder would release a
            // key the map no longer knows, and trap there rather than here.
            const Value& held = entry->second;
            assert(held.mReach.mHolds == 0 && "an entry abandoned while something holds it");
            mReached -= held.mReach.mEpoch == mPass.mEpoch ? 1 : 0;

            mKnown.erase(entry);
            mAbandoned = true;
        }

        /// Whether the walk in progress met every entry the map holds and abandoned none — so a
        /// sweep would erase nothing, and every slot the map names still stands.
        bool whole() const
        {
            // A count from an earlier epoch says nothing about this one, which has reached nothing
            // yet: only a map with nothing unheld in it is whole then.
            if (mCountedEpoch != mPass.mEpoch)
                return mKnown.size() == mHeld;

            return !mAbandoned && mReached + mHeld == mKnown.size();
        }

        /// Drops every entry neither the epoch nor a hold keeps, handing `drop` what each held on
        /// its way out, and says how many went. Skipped where the map is whole, which is the point
        /// of the count.
        template <class Drop>
        std::uint32_t retire(Drop drop)
        {
            if (whole())
            {
                // Two counters stand for the walk the skip saves, and one that drifts skips a sweep
                // with entries to drop: rows kept for as long as the world stands, with no symptom.
                assert(std::all_of(mKnown.begin(), mKnown.end(), [&](const auto& entry) { return keeps(entry.second); })
                    && "a map counted whole with an entry neither the epoch nor a hold keeps");
                return 0;
            }

            std::uint32_t dropped = 0;
            for (auto entry = mKnown.begin(); entry != mKnown.end();)
            {
                if (keeps(entry->second))
                {
                    ++entry;
                    continue;
                }

                drop(entry->second);
                entry = mKnown.erase(entry);
                ++dropped;
            }

            settle();
            return dropped;
        }

        /// The same where the entry holds nothing to give back.
        std::uint32_t retire()
        {
            return retire([](const auto&) {});
        }

        /// Hands `drop` every entry, whatever keeps it, and forgets them all: what an owner that
        /// goes does with what its entries hold on the scene.
        template <class Drop>
        void clear(Drop drop)
        {
            for (auto& entry : mKnown)
                drop(entry.second);

            mKnown.clear();
            mHeld = 0;
            settle();
        }

    private:
        /// Whether a sweep keeps `held`: met this epoch, or held by something.
        bool keeps(const Value& held) const { return held.mReach.mHolds != 0 || held.mReach.mEpoch == mPass.mEpoch; }

        /// Starts the count again where the epoch has moved on since it was last touched.
        void freshen()
        {
            if (mCountedEpoch == mPass.mEpoch)
                return;

            mCountedEpoch = mPass.mEpoch;
            mReached = 0;
            mAbandoned = false;
        }

        /// What a sweep leaves behind: every entry still here carries this epoch's stamp or a hold,
        /// and every slot the map names stands.
        void settle()
        {
            mCountedEpoch = mPass.mEpoch;
            mReached = mKnown.size() - mHeld;
            mAbandoned = false;
        }

        Map mKnown;
        const MirrorPass& mPass;

        std::uint64_t mCountedEpoch = 0;
        std::size_t mReached = 0;

        /// How many entries carry a hold. Kept across epochs, unlike `mReached`: a hold is not a
        /// fact about a walk.
        std::size_t mHeld = 0;

        bool mAbandoned = false;
    };

    /// What the scene knows one `osg` object as, keyed so the object cannot go while the entry
    /// stands. See `ByAddress`.
    template <class T, class Held = Known>
    using Identity = Kept<boost::unordered_flat_map<osg::ref_ptr<T>, Held, ByAddress<T>, ByAddress<T>>>;
}
