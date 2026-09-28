#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_NORMALDECODE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_NORMALDECODE_GLSL

// What a normal map's stored texel is as a tangent-space normal.
//
// **Its own file because two passes decode one texel and must agree**: the trace, reading a map
// at the level its cone resolves, and the spread measured on the map as it arrives. A spread taken
// off another decoding would be the spread of another map.

/// `2 rgb - 1`, as `objects.frag` decodes it, and not unit.
///
/// **A map of two channels reads nought in blue**, and no map of three holds that: its blue is the
/// normal's height over the surface, from a half up. So a blue of nought is a BC5 or an RG map, and
/// the third is rebuilt from the two, as the rasterizer rebuilds it for exactly those formats.
vec3 decodeNormal(vec3 stored)
{
    const vec2 across = stored.xy * 2.0 - 1.0;
    const float up = stored.z > 0.0 ? stored.z * 2.0 - 1.0 : sqrt(max(1.0 - dot(across, across), 0.0));

    return vec3(across, up);
}
#endif
