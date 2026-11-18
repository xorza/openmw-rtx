#pragma once

#include <QString>
#include <osg/Referenced>

#include "mask.hpp"

namespace CSVRender
{
    struct WorldspaceHitResult;

    class TagBase : public osg::Referenced
    {
        Mask mMask;

    public:
        explicit TagBase(Mask mask)
            : mMask(mask)
        {
        }

        Mask getMask() const { return mMask; }

        virtual QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const { return {}; }
    };
}
