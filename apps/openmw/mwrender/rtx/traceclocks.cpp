#include "traceclocks.hpp"

#include <cassert>

namespace MWRender
{
    TracedSeconds TraceClocks::trace(const double host, const double world)
    {
        const std::optional<Stamp> last = mLast;
        mLast = Stamp{ .mHost = host, .mWorld = world };
        if (!last.has_value())
            return TracedSeconds{};

        // Neither clock runs back: the loop's only advances, and the world's advances by the loop's step times
        // the time scale, which `DateTimeManager::setSimulationTimeScale` keeps at nought or over.
        assert(host >= last->mHost);
        assert(world >= last->mWorld);
        return TracedSeconds{
            .mHost = static_cast<float>(host - last->mHost),
            .mWorld = static_cast<float>(world - last->mWorld),
        };
    }
}
