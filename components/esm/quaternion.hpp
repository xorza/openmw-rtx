#pragma once

#include <osg/Quat>

namespace ESM
{
    // format 0, savegames only
    struct Quaternion
    {
        float mValues[4];

        Quaternion() = default;

        // OSG's quaternion converts to the record's.
        // NOLINTNEXTLINE(google-explicit-constructor)
        Quaternion(const osg::Quat& q)
            : mValues{
                static_cast<float>(q.w()),
                static_cast<float>(q.x()),
                static_cast<float>(q.y()),
                static_cast<float>(q.z()),
            }
        {
        }

        // OSG's quaternion converts to the record's.
        // NOLINTNEXTLINE(google-explicit-constructor)
        operator osg::Quat() const { return osg::Quat(mValues[1], mValues[2], mValues[3], mValues[0]); }
    };
}
