#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_NORMALDECODE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_NORMALDECODE_GLSL

// What a normal map's stored texel is as a tangent-space normal.
//
// **Its own file because two passes decode one texel and must agree**: the trace, reading a map at the
// level its cone resolves, and the spread measured on the map as it arrives.

/// `2 rgb - 1`, as `objects.frag` decodes it, and not unit: every normal map is made in one encoding, a unit
/// normal in three channels (`ChainBuild`), and a map of two stands in until it is.
vec3 decodeNormal(vec3 stored)
{
    return stored * 2.0 - 1.0;
}
#endif
