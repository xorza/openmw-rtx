#include "chainkeys.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <span>

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

    const osg::StateSet* ChainKeys::join(
        const osg::StateSet* const above, const osg::StateSet& link, const bool animated, const bool states)
    {
        // **A controller's state set is its own key**: `MaterialResolver::animate` keeps one per
        // node and rewrites it in place, so its address is already the placement's, and a material
        // under it is read off the whole chain on every frame. Paired with the chain above it, a
        // node whose own state set a controller swaps would be a new material at every swap.
        if (animated)
            return &link;
        if (!states)
            return above;
        return above != nullptr ? under(*above, link) : &link;
    }

    const osg::StateSet* ChainKeys::keyOf(const std::span<const osg::StateSet* const> stating)
    {
        const osg::StateSet* key = nullptr;
        for (const osg::StateSet* const link : stating)
            key = join(key, *link, false, true);
        return key;
    }

    void ChainKeys::retire(Released& released)
    {
        // Until nothing goes, because a pair that went let go of the key above it, which may then
        // be held by this table alone. A chain is a handful of pairs deep.
        //
        // **What the sink holds of a key is no hold on it**: the sink is what the frame let go of,
        // a material's key among it, waiting for its owner to hand it over. Counted again each
        // pass, because a pair that goes puts its own into it.
        for (std::size_t gone = 1; gone != 0;)
        {
            mReleasedScratch.clear();
            for (const osg::ref_ptr<const osg::Referenced>& object : released.get())
                mReleasedScratch.push_back(object.get());
            std::ranges::sort(mReleasedScratch);

            gone = boost::unordered::erase_if(mKeys, [&](const auto& entry) {
                const Held& held = entry.second;
                const auto inSink
                    = std::ranges::equal_range(mReleasedScratch, static_cast<const osg::Referenced*>(held.mKey.get()));
                if (held.mKey->referenceCount() - static_cast<int>(inSink.size()) != 1)
                    return false;

                released.keep(held.mAbove);
                released.keep(held.mOwn);
                released.keep(held.mKey);
                return true;
            });
        }
    }
}
