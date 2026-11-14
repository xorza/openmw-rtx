#pragma once

#include "../mwworld/livecellref.hpp"
#include "../mwworld/ptr.hpp"

#include <components/esm/path.hpp>
#include <components/esm4/loadligh.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWClass
{
    template <class Class>
    VFS::Path::NormalizedView getClassModel(const MWWorld::ConstPtr& ptr)
    {
        const MWWorld::LiveCellRef<Class>* ref = ptr.get<Class>();
        return ref->mBase->mModel.getNormalized();
    }
}
