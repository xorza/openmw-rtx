#ifndef OPENMW_COMPONENTS_RTX_SHADERS_TANGENT_H
#define OPENMW_COMPONENTS_RTX_SHADERS_TANGENT_H

#include "hosttypes.h"
#include "octahedral.h"
#include "portable.h"
#include "scene.h"

// A vertex's tangent and normal words, on the octahedral map: written once for both sides, because
// the host packs what the device unpacks and the skin pass packs again.

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
    ///
    /// **Selected and not branched**: three corners a hit decode, and a warp's lanes land on meshes
    /// with and without. The word nought decodes to a finite direction, which the select drops.
    RTX_SHADER vec4 unpackTangent(uint packed)
    {
        const vec3 unit = octahedralFromCode(packed & TANGENT_COORDINATE_MASK,
            (packed >> TANGENT_COORDINATE_BITS) & TANGENT_COORDINATE_MASK, TANGENT_STEPS, TANGENT_STEP);
        const vec4 tangent = vec4(unit, (packed & TANGENT_FLIPPED) != 0u ? -1.0f : 1.0f);
        return (packed & TANGENT_PRESENT) != 0u ? tangent : vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    /// A vertex's normal as one word: the tangent's word with no handedness, so both directions a
    /// vertex carries are one format, and a normal of no length — "the triangle's plane" — the word
    /// nought.
    RTX_SHADER uint packNormal(vec3 direction)
    {
        return packTangent(direction, false);
    }

    /// The unit normal `packed` holds, or nought where it holds none.
    RTX_SHADER vec3 unpackNormal(uint packed)
    {
        const vec4 tangent = unpackTangent(packed);
        return vec3(tangent[0], tangent[1], tangent[2]);
    }

#ifdef RTX_HOST
}
#endif

#endif
