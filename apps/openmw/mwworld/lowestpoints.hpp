#pragma once

#include <set>
#include <unordered_map>

namespace MWWorld
{
    /// The lowest of one height per object, read without a scan. It holds one entry per object it was given.
    class LowestPoints
    {
    public:
        /// Adds the object or moves it. A NaN has no order, so it leaves the object with no height.
        void set(const void* object, float z);

        /// An object it does not hold is left as it is.
        void remove(const void* object);

        void clear();

        /// `std::numeric_limits<float>::max()` when it holds nothing.
        float lowest() const;

    private:
        std::multiset<float> mHeights;
        std::unordered_map<const void*, std::multiset<float>::iterator> mHeightOf;
    };
}
