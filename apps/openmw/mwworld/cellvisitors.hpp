#pragma once

#include <string>
#include <vector>

#include "ptr.hpp"

namespace MWWorld
{
    struct ListAndResetObjectsVisitor
    {
        std::vector<MWWorld::Ptr> mObjects;

        bool operator()(const MWWorld::Ptr& ptr)
        {
            if (ptr.getRefData().getBaseNode())
            {
                ptr.getRefData().setBaseNode(nullptr);
            }
            mObjects.push_back(ptr);

            return true;
        }
    };

}
