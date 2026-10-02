#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Rtx
{
    /// An open-addressed table of indices with linear probing, a power of two long and never more
    /// than half full, so a probe always ends at an empty slot. The caller keys it by a hash and
    /// tells two entries apart itself, by what each slot's index names.
    class ProbeTable
    {
    public:
        static constexpr std::uint32_t sEmpty = ~std::uint32_t{ 0 };

        /// Empties the table and sizes it for `entries`, keeping the room it grew.
        void reset(const std::size_t entries)
        {
            const std::size_t slots = std::bit_ceil(std::max<std::size_t>(entries * 2, 16));
            mMask = slots - 1;
            mSlots.assign(slots, sEmpty);
        }

        /// Where a probe for `hash` begins, and the slot after `at` it goes on to.
        std::size_t first(const std::size_t hash) const { return hash & mMask; }
        std::size_t next(const std::size_t at) const { return (at + 1) & mMask; }

        std::uint32_t& operator[](const std::size_t at) { return mSlots[at]; }
        std::uint32_t operator[](const std::size_t at) const { return mSlots[at]; }

    private:
        std::vector<std::uint32_t> mSlots;
        std::size_t mMask = 0;
    };
}
