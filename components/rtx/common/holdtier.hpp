#pragma once

#include <cstddef>
#include <cstdint>

namespace Rtx
{
    /// **Who wants a row, most protected first**: what a hold on a scene's row states, and what residency plans by.
    /// A row's tier is the best tier any hold on it states (`HeldRows::getTier`).
    enum class HoldTier : std::uint8_t
    {
        /// A picture's scene, the sky's sheets, a holder that reads the texture itself.
        Forced,

        /// A placement of the walk, a cell inside the drawn reach, the band a load waits on.
        Shown,

        /// A cell of a predicted band.
        Hidden,

        /// What the walk remembers and no placement names: a model met before, a cell past the bands and inside the
        /// kept ring, the exterior while the eye is indoors.
        Kept,

        /// Nothing names it: what rests. No hold states it.
        Unheld,
    };

    /// The tiers a hold may state, `Unheld` not among them.
    inline constexpr std::size_t sHoldTierCount = static_cast<std::size_t>(HoldTier::Unheld);
}
