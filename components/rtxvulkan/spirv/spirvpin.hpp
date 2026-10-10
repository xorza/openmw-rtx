#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Rtx
{
    /// The module with every float operation's result fixed by the Vulkan specification rather than
    /// by whichever compile of it the driver runs. The build puts every shader through this between
    /// the optimizer and `spirv-val`, so the modules any renderer reads are already pinned.
    ///
    /// **Why the build decides and not the driver.** Vulkan lets a compile evaluate `OpDot`, the
    /// matrix products, `length`, `distance`, `normalize`, `cross`, `mix`, `smoothstep`, `reflect`,
    /// `refract`, `faceforward`, `asin`, `acos` and `mod` in any order the arithmetic allows, and
    /// fuse or reassociate any add or multiply that is not `NoContraction`. The NVIDIA driver
    /// compiles every ray-tracing launch a second time, from counters it keeps in its disk cache,
    /// and swaps the second code in at a frame of some processes; that code took the freedoms
    /// otherwise — one dot product summed z, y, x in the first code and x, y, z in the second — and
    /// the depth, the motion and the light of every frame after it moved in the last bit.
    ///
    /// **So the module takes the freedoms away.** Each of those operations becomes its own steps in
    /// one order, stated below. A multiply that nothing reads but one add or subtract becomes one
    /// `OpFmaKHR` with it, which the specification rounds once, correctly, and so to one value — an
    /// explicit `fma` in the source included. Every other add, subtract, multiply and divide is
    /// `NoContraction`, which forbids fusing and reordering it. The build chooses the
    /// fusions, so the pinned module is as fast as the one the driver fused for itself.
    ///
    /// **Left to the device, because no Vulkan instruction computes them exactly:** division, `sqrt`,
    /// `inversesqrt`, the exponentials, logarithms, powers and trigonometry, which the specification
    /// bounds by an error rather than fixes; packing and unpacking, whose precision it leaves to the
    /// implementation; and whether a denormal is flushed, which an NVIDIA device lets no module
    /// choose. The pinned forms keep each of these as one operation on pinned operands.
    ///
    /// **Left to the device even where its operands are constants.** `glslc` folds a constant
    /// expression into the module, but an operand that is constant only once the driver unrolls a
    /// loop or inlines a call the driver folds itself, to a value it does not round correctly and that
    /// moves with the rest of the module: an edit elsewhere in `shadowtiles.comp` moved three of its
    /// kernel's `exp` weights by an ulp. Such a value is the build's to write (`shadowLocalWeight`).
    ///
    /// **And the module keeps its signed zeros, infinities and NaNs.** Vulkan lets a compile assume a
    /// float is none of them unless the entry point says otherwise, and so fold `isnan` to false or
    /// `!(a >= b)` to `a < b` — which every guard against a value from the world is written as. Each
    /// entry point declares `SignedZeroInfNanPreserve` for every float width the module has, and the
    /// device is required to honour it.
    ///
    /// **A multiply a `precise` variable reads stays a multiply.** `precise` is the source's own
    /// `NoContraction`, and where two shaders must compute one value to the bit it is what says so:
    /// a fusion that depended on how each module happened to be optimised could split them.
    ///
    /// The orders, over components 0 up and a matrix's columns 0 up; the fusions follow from the
    /// rule above.
    /// - `dot(a, b)`: `a0 b0`, then `fma(ai, bi, sum)`.
    /// - `M v`: row i is `dot` of the row with `v`; `v M` is `dot(v, column)`; `M N` is `M` times each
    ///   column of `N`.
    /// - `length(v)` is `sqrt(dot(v, v))` and `|v|` of a scalar; `distance(a, b)` is `length(a - b)`;
    ///   `normalize(v)` is `v inversesqrt(dot(v, v))` and `sign(v)` of a scalar.
    /// - `cross(a, b)`: `fma(-a2, b1, a1 b2)`, `fma(-a0, b2, a2 b0)`, `fma(-a1, b0, a0 b1)`.
    /// - `mix(x, y, a)`: `fma(y, a, x (1 - a))`.
    /// - `smoothstep(e0, e1, x)`: `t t fma(-2, t, 3)` for `t = clamp((x - e0) / (e1 - e0), 0, 1)`.
    /// - `reflect(i, n)`: `fma(-k, n, i)` for `k = 2 dot(n, i)`.
    /// - `refract(i, n, eta)`: `d = dot(n, i)`, `k = fma(-eta eta, fma(-d, d, 1), 1)`, and nought
    ///   where `k < 0`, else `fma(-(fma(eta, d, sqrt(k))), n, eta i)`.
    /// - `faceforward(n, i, r)`: `n` where `dot(r, i) < 0`, else `-n`.
    /// - `asin(x)` is `atan2(x, sqrt(fma(-x, x, 1)))`; `acos(x)` is `atan2(sqrt(fma(-x, x, 1)), x)`.
    /// - `mod(x, y)` is `fma(-y, floor(x / y), x)`, and `rem` the same with `trunc`.
    /// - `round` is `roundEven`, and the float `min`, `max` and `clamp` are `NMin`, `NMax` and
    ///   `NClamp`: where one operand is a NaN, the other, which the F forms leave to the compile.
    /// - An add, subtract, multiply, divide or fused multiply-add of 32-bit constants, as the fusion
    ///   leaves them, is its value, correctly rounded here, where a driver folds what it is handed its
    ///   own way; not where an operand or the value is subnormal or a NaN, which stay the device's.
    /// - `x / c` for a constant `c` is `x (1 / c)`, the reciprocal rounded here: the quotient exactly
    ///   where `c` is a power of two and within an ulp and a half elsewhere, against the two and a
    ///   half a division is owed, and one answer on every device. Not where `c` or `1 / c` is not
    ///   a normal float.
    ///
    /// Throws `std::runtime_error` for a module it cannot read and for an operation it cannot pin —
    /// a derivative or a level of detail worked out from one, a relaxed precision, a rounding mode or
    /// any other float environment the module sets for itself, an
    /// extended instruction set it does not know, an instruction newer than the SPIR-V headers it
    /// was built with — naming it, so the build stops rather than ships a shader a compile may change.
    std::vector<std::uint32_t> pinFloatArithmetic(std::span<const std::uint32_t> module);

    /// The module with every rounding add, subtract, multiply and divide `NoContraction`, beside a
    /// `UserSemantic` that says the guard put it there: what the build hands the optimizer, which
    /// `pinFloatArithmetic` then reads as the source's arithmetic, the guard's marks taken off.
    ///
    /// **Because the optimizer's folds reach the module before the pinning holds it.** `glslc -O`
    /// rewrote `floor((m - 0.5) + 0.5)` as `floor(m)`, and `(x c1) c2` as `x (c1 c2)` is the same
    /// rule: arithmetic the source does not state, decided before anything pinned it. A
    /// `NoContraction` operation is one the optimizer folds no further, and the guard's are none
    /// the source asked for, so the pinning still fuses them where it would have.
    ///
    /// **The rounding operations alone**, and not the extended set: the optimizer folds an
    /// exponential of constants on the host, exactly, where left in the module the driver folds it
    /// its own way (`shadowLocalWeight`).
    ///
    /// Throws `std::runtime_error` for a module older than SPIR-V 1.4, which has no `UserSemantic`.
    std::vector<std::uint32_t> guardFloatArithmetic(std::span<const std::uint32_t> module);
}
