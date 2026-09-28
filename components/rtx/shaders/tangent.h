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
        if (!(abs(direction[0]) + abs(direction[1]) + abs(direction[2]) > 0.0f))
            return 0u;

        const vec2 square = octahedralSquare(direction);
        return TANGENT_PRESENT | (flipped ? TANGENT_FLIPPED : 0u) | octahedralStep(square[0], TANGENT_STEPS)
            | (octahedralStep(square[1], TANGENT_STEPS) << TANGENT_COORDINATE_BITS);
    }

    /// The unit direction and the handedness `packed` holds, or nought where it holds none.
    RTX_SHADER vec4 unpackTangent(uint packed)
    {
        if ((packed & TANGENT_PRESENT) == 0u)
            return vec4(0.0f, 0.0f, 0.0f, 0.0f);

        const vec2 square = vec2(octahedralCoordinate(packed & TANGENT_COORDINATE_MASK, TANGENT_STEPS),
            octahedralCoordinate((packed >> TANGENT_COORDINATE_BITS) & TANGENT_COORDINATE_MASK, TANGENT_STEPS));
        return vec4(octahedralUnit(square), (packed & TANGENT_FLIPPED) != 0u ? -1.0f : 1.0f);
    }

#ifdef RTX_HOST
}
#endif

#endif
