#pragma once

#include "action.hpp"
#include "ptr.hpp"

namespace MWWorld
{
    class ActionHarvest : public Action
    {
        void executeImp(const MWWorld::Ptr& actor) override;

    public:
        explicit ActionHarvest(const Ptr& container);
        ///< \param container The Container the Player has activated.
    };
}
