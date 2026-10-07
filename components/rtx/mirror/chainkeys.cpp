#include "chainkeys.hpp"

#include <cstddef>
#include <functional>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/StateSet>
#include <osg/ref_ptr>

namespace Rtx
{
    std::size_t ChainKeys::PairHash::operator()(const Pair& pair) const
    {
        const std::size_t above = std::hash<const osg::StateSet*>{}(pair.mAbove);
        return above
            ^ (std::hash<const osg::StateSet*>{}(pair.mOwn) + 0x9e3779b97f4a7c15ull + (above << 6) + (above >> 2));
    }

    const osg::StateSet* ChainKeys::under(const osg::StateSet& above, const osg::StateSet& stateSet)
    {
        const auto [entry, fresh] = mKeys.try_emplace(Pair{ .mAbove = &above, .mOwn = &stateSet });
        if (fresh)
            entry->second = Held{ .mAbove = &above, .mOwn = &stateSet, .mKey = new osg::StateSet };
        return entry->second.mKey.get();
    }

    void ChainKeys::retire()
    {
        // Until nothing goes, because a pair that went let go of the key above it, which may then
        // be held by this table alone. A chain is a handful of pairs deep.
        for (std::size_t gone = 1; gone != 0;)
            gone = boost::unordered::erase_if(
                mKeys, [](const auto& entry) { return entry.second.mKey->referenceCount() == 1; });
    }
}
