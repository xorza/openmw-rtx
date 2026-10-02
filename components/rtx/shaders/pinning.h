#ifndef OPENMW_COMPONENTS_RTX_SHADERS_PINNING_H
#define OPENMW_COMPONENTS_RTX_SHADERS_PINNING_H

#include "hosttypes.h"
#include "portable.h"

// What the pinning probe is handed and what it writes back. Included verbatim by both sides, for the
// reason `visibility.h` is.
//
// **The probe is the build's pinning put to the device.** `pinning.comp` computes each operation
// `Rtx::pinFloatArithmetic` rewrites, the host computes the order the rewrite states with one
// rounding per step and `std::fma` for each fusion, and the two have to agree to the bit — which
// they do only where the device rounds `OpFmaKHR` once and keeps every `NoContraction` step apart.
//
// **An operation with a core the device computes is checked around that core.** `normalize` has an
// inverse square root in it, `smoothstep` and `mod` a division, `refract` a square root, and no host
// computes those to the device's bit. So the probe writes the core as well, computed from the same
// pinned steps, and the host builds everything around it from the device's value: what is checked is
// every step the pinning fixed, and nothing the device was left.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint PINNING_WORKGROUP = 64;

    /// The probe's bindings: the cases in, the results out.
    const uint PINNING_BIND_CASES = 0;
    const uint PINNING_BIND_RESULTS = 1;

    /// Where each result stands among a case's: a vector's three components from its slot on, and
    /// beside an operation with a core the device computes, that core.
    const uint PINNING_DOT = 0;
    const uint PINNING_DOT_OF_FOUR = 1;
    const uint PINNING_CROSS = 2;
    const uint PINNING_MIX = 5;
    const uint PINNING_MATRIX = 8;
    const uint PINNING_REFLECT = 11;
    const uint PINNING_PRODUCT_AND_SUM = 14;
    const uint PINNING_FACEFORWARD = 15;
    const uint PINNING_NORMALIZE = 18;
    const uint PINNING_INVERSE_LENGTH = 21;
    const uint PINNING_SMOOTHSTEP = 22;
    const uint PINNING_QUOTIENT = 23;
    const uint PINNING_REFRACT = 24;
    const uint PINNING_ROOT = 27;
    const uint PINNING_MOD = 28;
    const uint PINNING_FLOOR = 29;

    /// The floats the probe writes for each case: one past the last slot.
    const uint PINNING_RESULTS = PINNING_FLOOR + 1;

    /// Three vectors, whose `xyz` are the operands and whose `w` are a fourth component, a blend and
    /// a vector the matrix is applied to.
    struct PinningCase
    {
        vec4 mA;
        vec4 mB;
        vec4 mC;
    };

    struct PinningConstants
    {
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(PinningCase) == 48, "PinningCase must be scalar-packed on every side");
    static_assert(sizeof(PinningConstants) == 4, "PinningConstants must be scalar-packed on every side");
}
#endif

#endif
