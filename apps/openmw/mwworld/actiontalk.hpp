#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionTalk : public Action
    {
        void executeImp(const Ptr& actor) override;

    public:
        explicit ActionTalk(const Ptr& actor);
        ///< \param actor The actor the player is talking to
    };
}
