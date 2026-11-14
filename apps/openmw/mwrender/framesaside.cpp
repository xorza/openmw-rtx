#include "framesaside.hpp"

#include <cassert>
#include <chrono>

namespace MWRender
{
    void FramesAside::openLoop(const TimePoint now)
    {
        if (mLoopOpened.has_value() && !mLoopDrew)
            mLoopsHidden += now - *mLoopOpened;
        mLoopOpened = now;
        mLoopDrew = false;
    }

    void FramesAside::openNested(const TimePoint now)
    {
        mLoopDrew = true;
        if (!mNestedFirst.has_value())
            mNestedFirst = now;
        mNestedOpened = now;
        mNestedLast = now;
    }

    void FramesAside::endNested(const TimePoint now, const NestedEnd end)
    {
        assert(mNestedFirst.has_value() && "an interface frame ended that never opened");
        mNestedLast = now;
        if (end == NestedEnd::Hidden)
            mNestedHidden += now - mNestedOpened;
    }

    AsideSpan FramesAside::takeAtWorldFrame()
    {
        const std::chrono::steady_clock::duration nested
            = mNestedFirst.has_value() ? mNestedLast - *mNestedFirst : std::chrono::steady_clock::duration::zero();
        const AsideSpan span{ .mInterface = nested - mNestedHidden, .mHidden = mLoopsHidden + mNestedHidden };
        assert(span.mInterface >= std::chrono::steady_clock::duration::zero());

        mNestedFirst.reset();
        mNestedHidden = std::chrono::steady_clock::duration::zero();
        mLoopsHidden = std::chrono::steady_clock::duration::zero();
        mLoopDrew = true;
        return span;
    }
}
