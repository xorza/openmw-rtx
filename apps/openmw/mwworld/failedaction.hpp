#pragma once

#include "action.hpp"
#include "ptr.hpp"

namespace MWWorld
{
    class FailedAction : public Action
    {
        std::string_view mMessage;

        void executeImp(const Ptr& actor) override;

    public:
        explicit FailedAction(std::string_view msg = {}, const Ptr& target = Ptr());
    };
}
