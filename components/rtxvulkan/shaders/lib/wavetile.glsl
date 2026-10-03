#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WAVETILE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WAVETILE_GLSL

// What a texel of a wave tile holds, which the swell's column pass and the wake's compose both
// write and `sea.glsl` samples: one statement of the layout, so the two fields cannot come to store
// it differently.

/// The surface texel: the slope, its squared length and the height's square, so a footprint's
/// filtered fetch carries the variances the sea's lobe is widened by.
vec4 waveSurfaceTexel(float height, vec2 slope)
{
    return vec4(slope, dot(slope, slope), height * height);
}

/// The curvature texel: the height's second derivatives along x, along y and across.
vec4 waveCurvatureTexel(vec3 curve)
{
    return vec4(curve, 0.0);
}

#endif
