#ifndef OPENMW_COMPONENTS_RTX_SHADERS_TANGENT_H
#define OPENMW_COMPONENTS_RTX_SHADERS_TANGENT_H

#include "hosttypes.h"
#include "octahedral.h"
#include "portable.h"
#include "scene.h"

// A vertex's tangent word, on the octahedral map: written once for both sides, because the host
// packs what the device unpacks and the skin pass packs again.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The word for `direction`, with the bitangent's handedness `flipped`, or nought — no tangent —
    /// for a direction of no length, or of none a number can hold.
    RTX_SHADER uint packTangent(vec3 direction, bool flipped)
    {
        if (!octahedralDirected(direction))
            return 0u;

        const uvec2 code = octahedralCode(direction, TANGENT_STEPS);
        return TANGENT_PRESENT | (flipped ? TANGENT_FLIPPED : 0u) | code[0] | (code[1] << TANGENT_COORDINATE_BITS);
    }

    /// The unit direction and the handedness `packed` holds, or nought where it holds none.
    RTX_SHADER vec4 unpackTangent(uint packed)
    {
        if ((packed & TANGENT_PRESENT) == 0u)
            return vec4(0.0f, 0.0f, 0.0f, 0.0f);

        const vec3 unit = octahedralFromCode(packed & TANGENT_COORDINATE_MASK,
            (packed >> TANGENT_COORDINATE_BITS) & TANGENT_COORDINATE_MASK, TANGENT_STEPS);
        return vec4(unit, (packed & TANGENT_FLIPPED) != 0u ? -1.0f : 1.0f);
    }

#ifdef RTX_HOST
}
#endif

#endif
