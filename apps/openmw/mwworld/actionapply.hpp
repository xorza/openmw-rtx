#pragma once

#include <string>

#include <components/esm/refid.hpp>

#include "action.hpp"

namespace MWWorld
{
    class ActionApply : public Action
    {
        ESM::RefId mId;

        void executeImp(const Ptr& actor) override;

    public:
        ActionApply(const Ptr& object, const ESM::RefId& id);
    };
}
