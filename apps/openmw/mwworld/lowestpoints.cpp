#include "lowestpoints.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace MWWorld
{
    void LowestPoints::set(const MWPhysics::Object* object, float z)
    {
        if (std::isnan(z))
        {
            remove(object);
            return;
        }

        const auto [found, isNew] = mHeightOf.try_emplace(object);
        if (isNew)
        {
            found->second = mHeights.insert(z);
            return;
        }

        // The node goes back in, so a platform a script moves every frame allocates nothing.
        auto node = mHeights.extract(found->second);
        node.value() = z;
        found->second = mHeights.insert(std::move(node));
    }

    void LowestPoints::remove(const MWPhysics::Object* object)
    {
        const auto found = mHeightOf.find(object);
        if (found == mHeightOf.end())
            return;
        mHeights.erase(found->second);
        mHeightOf.erase(found);
    }

    void LowestPoints::clear()
    {
        mHeightOf.clear();
        mHeights.clear();
    }

    float LowestPoints::lowest() const
    {
        return mHeights.empty() ? std::numeric_limits<float>::max() : *mHeights.begin();
    }
}
