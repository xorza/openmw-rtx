#ifndef OPENMW_COMPONENTS_RTX_SHADERS_OCTAHEDRAL_H
#define OPENMW_COMPONENTS_RTX_SHADERS_OCTAHEDRAL_H

#include "hosttypes.h"
#include "portable.h"

// A direction folded onto a square and back, and a coordinate of the square stepped to a count of
// its own: written once for both sides, because the host packs what the device unpacks and the
// device packs again, and a word two hands spelled is two words wherever they round apart.
//
// Scalar arithmetic over a vector's components, `v[i]`, which is the one spelling both languages
// read: GLSL's swizzles and OpenSceneGraph's accessors have none in common.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// A direction as a point of the square: Cigolle et al.'s octahedral map, which folds the lower
    /// hemisphere out over the upper one's corners so that the whole sphere is one square with no
    /// seam a filter would notice and no pole where precision runs out.
    ///
    /// @param direction of any length but nought, which has no direction and is the caller's to keep
    ///        apart.
    RTX_SHADER vec2 octahedralSquare(vec3 direction)
    {
        const float sum = abs(direction[0]) + abs(direction[1]) + abs(direction[2]);
        const float x = direction[0] / sum;
        const float y = direction[1] / sum;
        const bool upper = direction[2] >= 0.0f;

        // Selected and not branched, so a lane takes one path whichever half it is in.
        const float lowerX = (1.0f - abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float lowerY = (1.0f - abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        return vec2(upper ? x : lowerX, upper ? y : lowerY);
    }

    /// The point of the square back to the unit direction it stands for.
    RTX_SHADER vec3 octahedralUnit(vec2 square)
    {
        const float x = square[0];
        const float y = square[1];
        const float z = 1.0f - abs(x) - abs(y);
        const bool upper = z >= 0.0f;

        const float lowerX = (1.0f - abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float lowerY = (1.0f - abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        return normalize(vec3(upper ? x : lowerX, upper ? y : lowerY, z));
    }

    /// A coordinate of the square as one of `2 * steps + 1` values, nought for minus one, rounded half
    /// away from nought.
    ///
    /// **An odd count**, so that nought, one and minus one are exact: an axis-aligned direction comes
    /// back as the axis it was.
    RTX_SHADER uint octahedralStep(float value, uint steps)
    {
        const float scaled = clamp(value, -1.0f, 1.0f) * float(steps);
        const float rounded = scaled >= 0.0f ? floor(scaled + 0.5f) : -floor(0.5f - scaled);
        return uint(int(rounded) + int(steps));
    }

    RTX_SHADER float octahedralCoordinate(uint stored, uint steps)
    {
        return (float(stored) - float(steps)) / float(steps);
    }

#ifdef RTX_HOST
}
#endif

#endif
