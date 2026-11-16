#pragma once

#include <apps/openmw/mwworld/ptr.hpp>

namespace MWMechanics
{

    class Repair
    {
    public:
        void setTool(const MWWorld::Ptr& tool) { mTool = tool; }
        MWWorld::Ptr getTool() { return mTool; }

        void repair(const MWWorld::Ptr& itemToRepair);

    private:
        MWWorld::Ptr mTool;
    };

}
