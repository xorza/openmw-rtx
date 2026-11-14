#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionRepair : public Action
    {
        bool mForce;

        void executeImp(const Ptr& actor) override;

    public:
        /// @param item repair hammer
        explicit ActionRepair(const Ptr& item, bool force = false);
    };
}
