#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionEat : public Action
    {
        void executeImp(const Ptr& actor) override;

    public:
        explicit ActionEat(const MWWorld::Ptr& object);
    };
}
