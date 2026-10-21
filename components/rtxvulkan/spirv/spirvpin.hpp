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
    /// one order, stated below, and every add, subtract, multiply and divide is `NoContraction`,
    /// which forbids fusing and reordering it: each rounds once, correctly, and so to one value.
    ///
    /// **No fused multiply-add, which costs a module one instruction where a driver would have
    /// fused.** Only `SPV_KHR_fma` rounds a fusion once on every device, and AMD's Windows driver for
    /// RDNA 2 does not offer it. An explicit `fma` in the source is refused for the same reason,
    /// since the specification lets a compile round it once or twice.
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
    /// Throws `std::runtime_error` for a module it cannot read and for an operation it cannot pin —
    /// a derivative or a level of detail worked out from one, an `fma` or the capability to fuse, a
    /// relaxed precision, a rounding mode or any other float environment the module sets for itself, an
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
    /// `NoContraction` operation is one the optimizer folds no further. The pinning takes the
    /// guard's marks off, so a guarded module pins to the words the module itself pins to.
    ///
    /// **The rounding operations alone**, and not the extended set: the optimizer folds an
    /// exponential of constants on the host, exactly, where left in the module the driver folds it
    /// its own way (`shadowLocalWeight`).
    ///
    /// Throws `std::runtime_error` for a module older than SPIR-V 1.4, which has no `UserSemantic`.
    std::vector<std::uint32_t> guardFloatArithmetic(std::span<const std::uint32_t> module);
}
