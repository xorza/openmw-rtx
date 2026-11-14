#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionTake : public Action
    {
        void executeImp(const Ptr& actor) override;

    public:
        explicit ActionTake(const MWWorld::Ptr& object);
    };
}
