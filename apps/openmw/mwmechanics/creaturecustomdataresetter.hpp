#pragma once

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    struct CreatureCustomDataResetter
    {
        MWWorld::Ptr mPtr;

        ~CreatureCustomDataResetter()
        {
            if (!mPtr.isEmpty())
                mPtr.getRefData().setCustomData({});
        }
    };
}
