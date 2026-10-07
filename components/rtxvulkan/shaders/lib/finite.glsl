#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FINITE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FINITE_GLSL

// Whether a value is a number, read off its bits.
//
// **An integer test, where a float one is a test a compile may fold.** Every module declares the
// preservation of NaNs and infinities (`pinFloatArithmetic`), which makes `isnan` and `isinf` keep
// their meaning; this keeps it whatever a module declares, so the census `check` asserts on does not
// rest on the float environment alone.

/// Whether each component is a NaN or an infinity: its exponent all ones.
bvec4 notFinite(vec4 value)
{
    return greaterThanEqual(floatBitsToUint(value) & 0x7FFFFFFFu, uvec4(0x7F800000u));
}

#endif
