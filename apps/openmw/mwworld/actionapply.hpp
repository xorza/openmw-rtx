#pragma once

#include "action.hpp"
#include <components/esm/refid.hpp>
#include <string>

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
