#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Rtx
{
    /// **How many asks stand at each step of a scale, and the finest of them**: a multiset of small integers that
    /// answers its least in constant time, so an ask taken back as its asker moves away costs a count, never a pass
    /// over the askers. What a texture's want and a material's distance are kept as
    /// (`docs/architecture.md`, "A texture stands from the level its nearest user asks").
    template <std::size_t Steps>
    class LevelAsks
    {
    public:
        static_assert(Steps > 0 && Steps < 256, "a step is a byte");

        static constexpr std::size_t sSteps = Steps;

        /// One ask more at `step`.
        void ask(const std::size_t step)
        {
            assert(step < Steps && "an ask past the scale");
            ++mCounts[step];
            assert(mCounts[step] != 0 && "a count that wrapped");
            if (step < mFinest)
                mFinest = static_cast<std::uint8_t>(step);
        }

        /// One ask fewer at `step`, which one stood at.
        void unask(const std::size_t step)
        {
            assert(step < Steps && "an ask past the scale");
            assert(mCounts[step] > 0 && "an ask taken back that was never made");
            --mCounts[step];
            if (step != mFinest || mCounts[step] > 0)
                return;

            // The finest went: the next that holds a count, at most the scale's length away.
            std::size_t next = step + 1;
            while (next < Steps && mCounts[next] == 0)
                ++next;
            mFinest = static_cast<std::uint8_t>(next);
        }

        /// The finest step any ask stands at, or nothing where none stands.
        std::optional<std::uint8_t> finest() const
        {
            if (mFinest == Steps)
                return std::nullopt;
            return mFinest;
        }

        bool empty() const { return mFinest == Steps; }

        bool operator==(const LevelAsks&) const = default;

    private:
        std::array<std::uint32_t, Steps> mCounts{};
        std::uint8_t mFinest = Steps;
    };
}
