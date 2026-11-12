#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Rtx
{
    /// What a submit may still be reading, each entry with the value of the submit that last
    /// does, in stamp order (the queue takes submits in order), so what a wait lets go of is a
    /// prefix.
    ///
    /// A ring, so letting go of the oldest moves nothing that stays. It grows by doubling only when
    /// a frame holds more than the busiest before it, and never shrinks: a frame path does not allocate.
    template <class T>
    class Retiring
    {
    public:
        /// Holds `what` until the submit stamped `until` is done. **Stamps never fall**: one that did
        /// would stand behind an entry a wait has not reached, and outlive what it held.
        void hold(const std::uint64_t until, T&& what)
        {
            assert((mCount == 0 || at(mCount - 1).mUntil <= until) && "a stamp under one held before it");
            if (mCount == mHeld.size())
                grow();
            at(mCount) = Held{ until, std::move(what) };
            ++mCount;
        }

        /// Hands `let` every entry stamped at or below `finished`, in order, and forgets it.
        template <class Let>
        void releaseThrough(const std::uint64_t finished, Let&& let)
        {
            while (mCount > 0 && at(0).mUntil <= finished)
            {
                let(at(0).mObject);
                at(0) = Held{};
                mFirst = (mFirst + 1) % mHeld.size();
                --mCount;
            }
        }

        std::size_t size() const { return mCount; }

    private:
        struct Held
        {
            std::uint64_t mUntil = 0;
            T mObject{};
        };

        Held& at(const std::size_t index) { return mHeld[(mFirst + index) % mHeld.size()]; }
        const Held& at(const std::size_t index) const { return mHeld[(mFirst + index) % mHeld.size()]; }

        void grow()
        {
            std::vector<Held> larger(std::max<std::size_t>(16, mHeld.size() * 2));
            for (std::size_t index = 0; index < mCount; ++index)
                larger[index] = std::move(at(index));
            mHeld = std::move(larger);
            mFirst = 0;
        }

        std::vector<Held> mHeld;
        std::size_t mFirst = 0;
        std::size_t mCount = 0;
    };
}
