#include "historylength.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Rtx
{
    float HistoryLength::advance(const float sinceLast, const float seconds)
    {
        assert(sinceLast >= 0.0f);
        assert(seconds > 0.0f);

        // NRD's low pass: the previous frame rate times its time constant is how many frames the step is spread
        // over, so the smoothing spans 0.2 seconds at any rate.
        if (sinceLast > 0.0f)
        {
            const float spread = 0.2f / mFrameSeconds;
            mFrameSeconds += (sinceLast - mFrameSeconds) / (1.0f + spread);
        }

        return std::max(std::floor(seconds / mFrameSeconds + 0.5f), 1.0f);
    }
}
