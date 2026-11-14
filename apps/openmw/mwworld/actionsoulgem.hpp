#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionSoulgem : public Action
    {
        void executeImp(const MWWorld::Ptr& actor) override;

    public:
        /// @param soulgem to use
        explicit ActionSoulgem(const Ptr& object);
    };
}
