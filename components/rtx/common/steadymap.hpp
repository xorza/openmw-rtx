#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace Rtx
{
    /// **A hash map whose load is its size over its slots, however much it churns**: a world's mirror fills and empties
    /// its tables cell by cell for as long as the game runs, and a table reserved for the most it holds at once must
    /// then never move its entries on a frame (`fillsRoom`).
    ///
    /// - **Linear probing with backward-shift deletion**: an erase moves the entries after it back into the gap, so no
    ///   tombstone is left and nothing lowers the room. Refused: Boost's open-addressing maps, whose anti-drift lowers
    ///   the maximum load on an erase from an overflowed group (`recover_slot`), so a table reserved for its peak
    ///   rehashed short of it under a long session's churn, and its rehash shrank it back to its size, after which it
    ///   rehashed again and again; a chained map, an allocation an insert.
    /// - **The entries dense in one array, the slots naming them**: iteration walks the array alone, and an erase moves
    ///   the last entry into the hole, so a loop that erases as it goes takes the entry `erase` answers and visits
    ///   each once. An erase or an insert may move any entry: nothing keeps an iterator across either.
    /// - **Half the slots at most**, so a probe stays short; past that the slots double, which is the growth a room
    ///   exists to prevent and `fillsRoom` counts.
    ///
    /// `Hash` and `Equal` may be one transparent type, as `ByAddress` is, so a lookup takes the raw key.
    template <class Key, class Value, class Hash = std::hash<Key>, class Equal = std::equal_to<>>
    class SteadyMap
    {
    public:
        using key_type = Key;
        using mapped_type = Value;
        /// The key fixed, as a standard map's is: a key changed in place would stand in the slot of another hash.
        using value_type = std::pair<const Key, Value>;
        using iterator = value_type*;
        using const_iterator = const value_type*;

        SteadyMap() = default;

        /// Room for `room` entries at once before the slots double: twice as many slots, and the entries' array, made
        /// once.
        void reserve(std::size_t room)
        {
            const std::size_t slots = std::bit_ceil(std::max<std::size_t>(2 * room, sMinSlots));
            if (slots > mSlots.size())
                regrow(slots);
        }

        std::size_t size() const { return mEntries.size(); }
        bool empty() const { return mEntries.empty(); }

        /// How many entries the slots hold before the next insert doubles them: half of them.
        std::size_t max_load() const { return maxLoad(); }

        iterator begin() { return mEntries.data(); }
        iterator end() { return mEntries.data() + mEntries.size(); }
        const_iterator begin() const { return mEntries.data(); }
        const_iterator end() const { return mEntries.data() + mEntries.size(); }

        template <class Lookup>
        iterator find(const Lookup& key)
        {
            const std::optional<std::size_t> slot = slotOf(key);
            return slot.has_value() ? begin() + mSlots[*slot].mEntry - 1 : end();
        }

        template <class Lookup>
        const_iterator find(const Lookup& key) const
        {
            const std::optional<std::size_t> slot = slotOf(key);
            return slot.has_value() ? begin() + mSlots[*slot].mEntry - 1 : end();
        }

        template <class Lookup>
        bool contains(const Lookup& key) const
        {
            return slotOf(key).has_value();
        }

        /// The entry for `key`, made from `args` where there is none, and whether this made it.
        template <class KeyArg, class... Args>
        std::pair<iterator, bool> try_emplace(KeyArg&& key, Args&&... args)
        {
            const std::uint64_t hash = hashOf(key);
            if (const std::optional<std::size_t> slot = slotOf(key, hash))
                return { begin() + mSlots[*slot].mEntry - 1, false };

            if (mEntries.size() >= maxLoad())
                regrow(std::max<std::size_t>(2 * mSlots.size(), sMinSlots));
            assert(mEntries.size() < std::numeric_limits<std::uint32_t>::max() && "more entries than a slot names");

            mEntries.emplace_back(std::piecewise_construct, std::forward_as_tuple(std::forward<KeyArg>(key)),
                std::forward_as_tuple(std::forward<Args>(args)...));
            place(hash, static_cast<std::uint32_t>(mEntries.size()));
            return { end() - 1, true };
        }

        /// Erases `at`, and answers the entry that stands where it stood: the last one moved into its place, or `end`.
        iterator erase(const_iterator at)
        {
            assert(at >= begin() && "an iterator before this map's entries");
            assert(at < end() && "an iterator past this map's entries");
            const auto index = static_cast<std::size_t>(at - begin());
            unslot(slotNaming(index));

            const std::size_t last = mEntries.size() - 1;
            if (index != last)
            {
                mSlots[slotNaming(last)].mEntry = static_cast<std::uint32_t>(index + 1);
                // Made again in place, as a fixed key is moved but never assigned.
                std::destroy_at(&mEntries[index]);
                std::construct_at(&mEntries[index], std::move(mEntries[last]));
            }
            mEntries.pop_back();
            return begin() + index;
        }

        /// Erases every entry `drop` answers true for, and says how many went.
        template <class Drop>
        std::size_t erase_if(Drop drop)
        {
            std::size_t erased = 0;
            for (iterator entry = begin(); entry != end();)
            {
                if (drop(*entry))
                {
                    entry = erase(entry);
                    ++erased;
                }
                else
                    ++entry;
            }
            return erased;
        }

        /// Forgets every entry, the slots and the array kept.
        void clear()
        {
            mEntries.clear();
            std::fill(mSlots.begin(), mSlots.end(), Slot{});
        }

    private:
        /// A slot: the entry it names, one past its index so nought is empty, and its key's hash, which says the slot
        /// a probe for it starts at.
        struct Slot
        {
            std::uint32_t mEntry = 0;
            std::uint32_t mHash = 0;
        };

        static constexpr std::size_t sMinSlots = 16;

        std::size_t maxLoad() const { return mSlots.size() / 2; }
        std::size_t mask() const { return mSlots.size() - 1; }

        /// The hash `Hash` answers, mixed so a key of aligned addresses or small integers spreads over every slot.
        template <class Lookup>
        std::uint64_t hashOf(const Lookup& key) const
        {
            const auto raw = static_cast<std::uint64_t>(Hash{}(key));
            return (raw ^ (raw >> 31)) * 0x9e3779b97f4a7c15ull;
        }

        std::size_t homeOf(std::uint32_t hash) const { return hash & mask(); }

        template <class Lookup>
        std::optional<std::size_t> slotOf(const Lookup& key) const
        {
            return slotOf(key, hashOf(key));
        }

        template <class Lookup>
        std::optional<std::size_t> slotOf(const Lookup& key, std::uint64_t hash) const
        {
            if (mSlots.empty())
                return std::nullopt;
            const auto fragment = static_cast<std::uint32_t>(hash >> 32);
            for (std::size_t slot = homeOf(fragment);; slot = (slot + 1) & mask())
            {
                const Slot& at = mSlots[slot];
                if (at.mEntry == 0)
                    return std::nullopt;
                if (at.mHash == fragment && Equal{}(mEntries[at.mEntry - 1].first, key))
                    return slot;
            }
        }

        /// The slot that names the entry at `index`, which the entry's key finds.
        std::size_t slotNaming(std::size_t index) const
        {
            const auto fragment = static_cast<std::uint32_t>(hashOf(mEntries[index].first) >> 32);
            for (std::size_t slot = homeOf(fragment);; slot = (slot + 1) & mask())
            {
                assert(mSlots[slot].mEntry != 0 && "an entry no slot names");
                if (mSlots[slot].mEntry == index + 1)
                    return slot;
            }
        }

        /// Names the entry `entry`, one past its index, in the first free slot from the hash's home.
        void place(std::uint64_t hash, std::uint32_t entry)
        {
            const auto fragment = static_cast<std::uint32_t>(hash >> 32);
            std::size_t slot = homeOf(fragment);
            while (mSlots[slot].mEntry != 0)
                slot = (slot + 1) & mask();
            mSlots[slot] = Slot{ .mEntry = entry, .mHash = fragment };
        }

        /// Empties `slot`, each entry after it in its run moved back while that brings it nearer its home.
        void unslot(std::size_t slot)
        {
            for (std::size_t next = (slot + 1) & mask();; next = (next + 1) & mask())
            {
                const Slot& at = mSlots[next];
                if (at.mEntry == 0)
                    break;
                // The gap stands between the entry's home and the entry: moving it there keeps it reachable.
                const std::size_t home = homeOf(at.mHash);
                const bool between = slot <= next ? (home <= slot || home > next) : (home <= slot && home > next);
                if (!between)
                    continue;
                mSlots[slot] = at;
                slot = next;
            }
            mSlots[slot] = Slot{};
        }

        void regrow(std::size_t slots)
        {
            mSlots.assign(slots, Slot{});
            for (std::size_t index = 0; index < mEntries.size(); ++index)
                place(hashOf(mEntries[index].first), static_cast<std::uint32_t>(index + 1));
            mEntries.reserve(maxLoad());
        }

        std::vector<value_type> mEntries;
        std::vector<Slot> mSlots;
    };
}
