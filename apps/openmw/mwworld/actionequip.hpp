#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionEquip : public Action
    {
        bool mForce;

        void executeImp(const Ptr& actor) override;

    public:
        /// @param item to equip
        explicit ActionEquip(const Ptr& object, bool force = false);
    };
}
