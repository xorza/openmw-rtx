#pragma once

#include <apps/openmw/mwworld/ptr.hpp>

namespace MWMechanics
{
    struct CreatureCustomDataResetter
    {
        MWWorld::Ptr mPtr;

        explicit CreatureCustomDataResetter(const MWWorld::Ptr& ptr)
            : mPtr(ptr)
        {
        }

        ~CreatureCustomDataResetter()
        {
            if (!mPtr.isEmpty())
                mPtr.getRefData().setCustomData({});
        }
        CreatureCustomDataResetter(const CreatureCustomDataResetter&) = delete;
        CreatureCustomDataResetter& operator=(const CreatureCustomDataResetter&) = delete;
    };
}
