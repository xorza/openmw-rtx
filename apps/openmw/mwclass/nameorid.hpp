#pragma once

#include <string_view>

#include <apps/openmw/mwworld/livecellref.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm/refid.hpp>

namespace MWClass
{
    template <class Class>
    std::string_view getNameOrId(const MWWorld::ConstPtr& ptr)
    {
        const MWWorld::LiveCellRef<Class>* ref = ptr.get<Class>();
        if (!ref->mBase->mName.empty())
            return ref->mBase->mName;
        if (const auto* id = ref->mBase->mId.template getIf<ESM::StringRefId>())
            return id->getValue();
        return {};
    }
}
