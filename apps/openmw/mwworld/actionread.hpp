#pragma once

#include "action.hpp"

namespace MWWorld
{
    class ActionRead : public Action
    {
        void executeImp(const MWWorld::Ptr& actor) override;

    public:
        /// @param book or scroll to read
        explicit ActionRead(const Ptr& object);
    };
}
