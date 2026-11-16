#pragma once

#include <apps/openmw/mwworld/livecellref.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
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
