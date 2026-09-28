#include "skycrossing.hpp"

#include <cassert>
#include <cstddef>

namespace RtxTool
{
    SkyCrossing::SkyCrossing(const std::uint32_t from, const std::uint32_t to, const float crossed)
        : mFrom(from)
        , mTo(to)
        , mCrossed(crossed)
    {
        assert(crossed >= 0.0f && "a crossing the world counts as not begun");

        // What the world counts as whole has landed.
        if (!isCrossing() || crossed >= 1.0f)
            settle(to);
    }

    void SkyCrossing::ask(const std::uint32_t weather)
    {
        if (weather == mTo)
            return;

        if (weather == mFrom || mCrossed >= 0.5f)
        {
            mFrom = mTo;
            mCrossed = 1.0f - mCrossed;
        }

        mTo = weather;

        // Turned round from where nothing had crossed yet, which stands at the weather it left.
        if (mCrossed >= 1.0f)
            settle(weather);
    }

    void SkyCrossing::settle(const std::uint32_t weather)
    {
        mFrom = weather;
        mTo = weather;
        mCrossed = 0.0f;
    }

    void SkyCrossing::advance(const float share)
    {
        assert(share >= 0.0f && "a crossing run backwards");

        if (!isCrossing())
            return;

        mCrossed += share;
        if (mCrossed >= 1.0f)
            settle(mTo);
    }

    std::size_t SkyCrossing::stepAmong(const std::span<const std::uint32_t> rolled, const int steps) const
    {
        assert(!rolled.empty() && "a step among no weathers");

        const auto count = static_cast<std::ptrdiff_t>(rolled.size());
        std::ptrdiff_t at = steps > 0 ? -1 : count;
        for (std::ptrdiff_t index = 0; index < count; ++index)
            if (rolled[static_cast<std::size_t>(index)] == mTo)
                at = index;

        return static_cast<std::size_t>(((at + steps) % count + count) % count);
    }
}
