#pragma once

#include "action.hpp"
#include "ptr.hpp"

namespace MWWorld
{
    class ActionDoor : public Action
    {
        void executeImp(const MWWorld::Ptr& actor) override;

    public:
        explicit ActionDoor(const Ptr& object);
    };
}
