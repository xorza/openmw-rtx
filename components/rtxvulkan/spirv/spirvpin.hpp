#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Rtx
{
    /// The module with every float operation's result fixed by the Vulkan specification rather than by whichever
    /// compile of it the driver runs. The build puts every shader through this between the optimizer and `spirv-val`,
    /// so the modules any renderer reads are already pinned.
    ///
    /// Why the build decides and not the driver: Vulkan lets a compile evaluate `OpDot`, the matrix products, `length`,
    /// `distance`, `normalize`, `cross`, `mix`, `smoothstep`, `reflect`, `refract`, `faceforward`, `asin`, `acos` and
    /// `mod` in any order the arithmetic allows, and fuse or reassociate any add or multiply that is not
    /// `NoContraction`. The NVIDIA driver recompiles every ray-tracing launch from counters in its disk cache and swaps
    /// the second code in at a frame of some processes; one dot product summed z, y, x in the first code and x, y, z in
    /// the second, and every frame after it moved in the last bit. So each of those becomes its own steps in one order,
    /// stated below, and every add, subtract, multiply and divide is `NoContraction`.
    ///
    /// No fused multiply-add, at one instruction more where a driver would have fused: only `SPV_KHR_fma` rounds a
    /// fusion once on every device, and AMD's Windows driver for RDNA 2 does not offer it. An explicit `fma` is refused
    /// for the same reason.
    ///
    /// Left to the device, because no Vulkan instruction computes them exactly: division, `sqrt`, `inversesqrt`,
    /// exponentials, logarithms, powers, trigonometry, packing and unpacking, and whether a denormal is flushed. Also
    /// where the operands are constants: a driver folds a value that is constant only after unrolling or inlining its
    /// own way (an edit elsewhere in `shadowtiles.comp` moved three `exp` weights by an ulp), so such a value is the
    /// build's to write (`shadowLocalWeight`).
    ///
    /// The module keeps its signed zeros, infinities and NaNs: each entry point declares `SignedZeroInfNanPreserve` for
    /// every float width it has, since a compile may otherwise fold `isnan` to false and `!(a >= b)` to `a < b`.
    ///
    /// The orders, over components 0 up and a matrix's columns 0 up, each product and sum rounded
    /// in turn.
    /// - `dot(a, b)`: `a0 b0`, then `sum + ai bi`.
    /// - `M v`: row i is `dot` of the row with `v`; `v M` is `dot(v, column)`; `M N` is `M` times each
    ///   column of `N`.
    /// - `length(v)` is `sqrt(dot(v, v))` and `|v|` of a scalar; `distance(a, b)` is `length(a - b)`;
    ///   `normalize(v)` is `v inversesqrt(dot(v, v))` and `sign(v)` of a scalar.
    /// - `cross(a, b)`: `a1 b2 - a2 b1`, `a2 b0 - a0 b2`, `a0 b1 - a1 b0`.
    /// - `mix(x, y, a)`: `x (1 - a) + y a`.
    /// - `smoothstep(e0, e1, x)`: `t t (3 - 2 t)` for `t = clamp((x - e0) / (e1 - e0), 0, 1)`.
    /// - `reflect(i, n)`: `i - k n` for `k = 2 dot(n, i)`.
    /// - `refract(i, n, eta)`: `d = dot(n, i)`, `k = 1 - eta eta (1 - d d)`, and nought where
    ///   `k < 0`, else `eta i - (eta d + sqrt(k)) n`.
    /// - `faceforward(n, i, r)`: `n` where `dot(r, i) < 0`, else `-n`.
    /// - `asin(x)` is `atan2(x, sqrt(1 - x x))`; `acos(x)` is `atan2(sqrt(1 - x x), x)`.
    /// - `mod(x, y)` is `x - y floor(x / y)`, and `rem` the same with `trunc`.
    /// - `round` is `roundEven`, and the float `min`, `max` and `clamp` are `NMin`, `NMax` and
    ///   `NClamp`: where one operand is a NaN, the other, which the F forms leave to the compile.
    /// - An add, subtract, multiply or divide of 32-bit constants is its value, correctly rounded
    ///   here, where a driver folds what it is handed its own way; not where an operand or the value
    ///   is subnormal or a NaN, which stay the device's.
    /// - `x / c` for a constant `c` is `x (1 / c)`, the reciprocal rounded here: the quotient exactly
    ///   where `c` is a power of two and within an ulp and a half elsewhere, against the two and a
    ///   half a division is owed, and one answer on every device. Not where `c` or `1 / c` is not
    ///   a normal float.
    ///
    /// Throws `std::runtime_error` for a module it cannot read and for an operation it cannot pin — a derivative or a
    /// level of detail worked out from one, an `fma` or the capability to fuse, a relaxed precision, a rounding mode or
    /// other float environment the module sets, an extended instruction set it does not know, an instruction newer than
    /// the SPIR-V headers it was built with, and any instruction that makes a float and that no rule here names —
    /// naming it, so the build stops rather than ships a shader a compile may change.
    std::vector<std::uint32_t> pinFloatArithmetic(std::span<const std::uint32_t> module);

    /// The module with every rounding add, subtract, multiply and divide `NoContraction`, beside a `UserSemantic` that
    /// says the guard put it there: what the build hands the optimizer, which `pinFloatArithmetic` then reads as the
    /// source's arithmetic, the guard's marks taken off. Because the optimizer's folds reach the module before the
    /// pinning holds it (`glslc -O` rewrote `floor((m - 0.5) + 0.5)` as `floor(m)`). The rounding operations alone: the
    /// optimizer folds an exponential of constants on the host, exactly, where the driver would fold it its own way
    /// (`shadowLocalWeight`).
    ///
    /// Throws `std::runtime_error` for a module older than SPIR-V 1.4, which has no `UserSemantic`.
    std::vector<std::uint32_t> guardFloatArithmetic(std::span<const std::uint32_t> module);
}
