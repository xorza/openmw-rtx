#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_INDIRECT_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_INDIRECT_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif
    /// Luminance is already weighted by 1/rays. Reference white is 1 after exposure,
    /// before tone mapping; scale the whole RGB sample, never its channels separately.
    RTX_SHADER float indirectSampleScale(float luminance, float multiple, float exposure, uint rays)
    {
        if (!(multiple > 0.0f) || !(luminance > 0.0f))
            return 1.0f;
        const float scale = multiple / (exposure * float(rays) * luminance);
        return scale < 1.0f ? scale : 1.0f;
    }
#ifdef RTX_HOST
}
#endif
#endif
