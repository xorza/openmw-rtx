#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionAlchemy : public Action
    {
        bool mForce;
        void executeImp(const Ptr& actor) override;

    public:
        explicit ActionAlchemy(bool force = false);
    };
}
