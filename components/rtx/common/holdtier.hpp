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

        /// What the game stands for itself: a placement of the walk, and the ground of a cell of its active grid. What
        /// every change of cells waits on, at a door, a load and a crossing (`MWRender::RunSetup::mStandsAtChange`).
        Near,

        /// A cell inside the drawn reach and past the active grid, its ground and its statics: what streams in behind a
        /// change of cells, and a tour's change waits on as well.
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
