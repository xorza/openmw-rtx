#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionOpen : public Action
    {
        void executeImp(const MWWorld::Ptr& actor) override;

    public:
        explicit ActionOpen(const Ptr& container);
        ///< \param container The Container the Player has activated.
    };
}
