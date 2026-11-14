#ifndef OPENMW_COMPONENTS_BULLETHELPERS_HEIGHTFIELD_H
#define OPENMW_COMPONENTS_BULLETHELPERS_HEIGHTFIELD_H

#include <LinearMath/btVector3.h>

namespace BulletHelpers
{
    inline btVector3 getHeightfieldShift(int x, int y, int size, float minHeight, float maxHeight)
    {
        return btVector3((static_cast<float>(x) + 0.5f) * static_cast<float>(size),
            (static_cast<float>(y) + 0.5f) * static_cast<float>(size), (maxHeight + minHeight) * 0.5f);
    }
}

#endif
