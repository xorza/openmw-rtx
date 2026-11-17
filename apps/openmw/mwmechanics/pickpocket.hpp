#pragma once

#include <cstdint>

#include <apps/openmw/mwworld/ptr.hpp>

namespace MWMechanics
{

    class Pickpocket
    {
    public:
        Pickpocket(const MWWorld::Ptr& thief, const MWWorld::Ptr& victim);

        /// Steal some items
        /// @return Was the thief detected?
        bool pick(const MWWorld::Ptr& item, std::int32_t count);
        /// End the pickpocketing process
        /// @return Was the thief detected?
        bool finish();

    private:
        bool getDetected(float valueTerm);
        float getChanceModifier(const MWWorld::Ptr& ptr, float add = 0);
        MWWorld::Ptr mThief;
        MWWorld::Ptr mVictim;
    };

}
