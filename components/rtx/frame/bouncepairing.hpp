#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Rtx
{
    /// One texel's step to its partner in a `BouncePairing`, across and down.
    struct PairingStep
    {
        std::int32_t mAcross = 0;
        std::int32_t mDown = 0;
    };

    /// A square of texels paired with each other, each texel's partner's partner itself, the steps
    /// between partners spread as a normal distribution of a given deviation: what the bounce's
    /// spatial reuse takes its neighbours from (Lin, Kettunen, Wyman 2026, *ReSTIR PT Enhanced*,
    /// §3). **A pair's two shifts are each other's**, so a pixel that reuses from its partner
    /// computes half of what both need, and the partner reads the other half.
    ///
    /// Made as the paper makes it: consecutive pixels share a link, and tiled 2×2 random shuffles,
    /// every other one moved diagonally by one and wrapping at the edges, walk the links apart.
    /// **The shuffles stop where the measured deviation reaches the one asked for**, rather than at a
    /// count from the paper's fit, so the deviation is what a test can hold. The texture tiles: a
    /// step is wrapped to the nearer copy of the partner, computed once for each pair and negated
    /// for the other end, so a pair is a pair on the torus whatever its length.
    class BouncePairing
    {
    public:
        /// @param size the side, even, so the first shuffle's blocks tile it.
        /// @param deviation the steps' deviation along each axis, from 0.8 (one shuffle) to a sixth
        ///        of the side, past which the wrapped steps stop spreading.
        /// @param seed what the shuffles draw from: one seed is one texture on every machine.
        BouncePairing(std::uint32_t size, float deviation, std::uint32_t seed);

        std::uint32_t getSize() const { return mSize; }

        /// The deviation of the steps along each axis, as made: at least the one asked for, and
        /// over it by what one more shuffle adds.
        float getDeviation() const { return mDeviation; }

        PairingStep stepAt(std::uint32_t across, std::uint32_t down) const;

        /// Every texel's step, row by row, as two signed sixteen-bit halves of a word, across in the
        /// low half: what the device reads.
        std::span<const std::uint32_t> getSteps() const { return mSteps; }

    private:
        std::uint32_t mSize;
        float mDeviation = 0.0f;
        std::vector<std::uint32_t> mSteps;
    };

    /// Both pairing textures for a frame `height` traced rows tall, the first's steps and then the
    /// second's: what the device reads. **The steps' deviation is the one whose mean distance a
    /// uniform disc of the reuse's radius has** (ReSTIR PT Enhanced §7): `σ √(π/2) = 2R/3`, so
    /// `σ = √(8 / 9π) R`, held where the shuffles still spread it.
    std::vector<std::uint32_t> bouncePairingSteps(std::uint32_t height);
}
