#include "classmasks.hpp"

#include <components/rtx/shaders/scene.h>

namespace MWRender
{
    ViewDescription describeView(const osg::Node::NodeMask cullMask)
    {
        std::uint32_t mask = 0;
        for (const ClassMask& held : sClassMasks)
            if ((cullMask & held.mNodes) != 0)
                mask |= Rtx::classBit(held.mClass);

        if ((cullMask & (Mask_Water | Mask_SimpleWater)) != 0)
            mask |= Rtx::Shaders::MASK_WATER;
        if ((cullMask & (Mask_ParticleSystem | Mask_WeatherParticles)) != 0)
            mask |= Rtx::Shaders::MASK_PARTICLE;

        // No `MASK_MEDIUM`: a medium is gathered by a ray that casts with that bit alone, and tested
        // against the camera's classes there; in the eye's own mask it would meet the shells of a
        // class left out.
        return ViewDescription{ .mRayMask = mask, .mLamps = (cullMask & Mask_Lighting) != 0 };
    }
}
