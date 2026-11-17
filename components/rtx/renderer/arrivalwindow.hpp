#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Rtx
{
    /// What one extension handed the device: its arriving files and the structures the device built, together, and
    /// the structures alone.
    struct ArrivalBytes
    {
        std::uint64_t mAll = 0;
        std::uint64_t mStructures = 0;
    };

    /// **The largest of the last `sCount` extensions**, each figure its own most: what the room kept for the next
    /// arrival is sized by (`SceneUploader`), and the step what rests comes down by. A window and not the session's
    /// most, which one heavy arrival held for good: a crossing's extensions each hand over a part of it, some twenty
    /// a crossing at a tour's speed, so the window remembers about three crossings.
    class ArrivalWindow
    {
    public:
        static constexpr std::size_t sCount = 64;

        /// Files `arrival` as the newest, the oldest forgotten.
        void note(const ArrivalBytes& arrival)
        {
            mArrivals[mNext] = arrival;
            mNext = (mNext + 1) % sCount;
        }

        ArrivalBytes largest() const
        {
            ArrivalBytes most;
            for (const ArrivalBytes& arrival : mArrivals)
            {
                most.mAll = std::max(most.mAll, arrival.mAll);
                most.mStructures = std::max(most.mStructures, arrival.mStructures);
            }
            return most;
        }

    private:
        std::array<ArrivalBytes, sCount> mArrivals{};
        std::size_t mNext = 0;
    };
}
