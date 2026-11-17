#pragma once

#include <cstdint>

#include "blendmask.hpp"
#include "bonegroup.hpp"

namespace MWRender
{
    /// Holds an animation priority value for each BoneGroup.
    struct AnimPriority
    {
        /// Convenience constructor, initialises all priorities to the same value.
        // One priority converts to the same priority for every part of a group.
        // NOLINTNEXTLINE(google-explicit-constructor)
        AnimPriority(std::int32_t priority)
        {
            for (std::int32_t& i : mPriority)
                i = priority;
        }

        bool operator==(const AnimPriority& other) const
        {
            for (std::uint32_t i = 0; i < sNumBlendMasks; ++i)
                if (other.mPriority[i] != mPriority[i])
                    return false;
            return true;
        }

        std::int32_t& operator[](BoneGroup n) { return mPriority[n]; }

        const std::int32_t& operator[](BoneGroup n) const { return mPriority[n]; }

        bool contains(std::int32_t priority) const
        {
            for (std::int32_t i : mPriority)
                if (priority == i)
                    return true;
            return false;
        }

        std::int32_t mPriority[sNumBlendMasks]{};
    };
}
