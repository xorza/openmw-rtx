#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_STRESS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_STRESS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the busy loop `check` appends to every frame is told. Included verbatim by
// both sides, for the reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `stress.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint STRESS_BIND_COUNTS = 0;
    const uint STRESS_BINDINGS = 1;

    /// Lanes in the one workgroup the loop runs on. One group and not a frame's worth, because
    /// what the loop is for is to hold the queue for a stated time, and one group does that with
    /// the rest of the card idle — the frame behind it is what the hold is supposed to overlap.
    const uint STRESS_WORKGROUP = 64;

    struct StressConstants
    {
        /// How long the loop holds, in ticks of the device's real-time clock, whose rate is the
        /// device's own: a nanosecond on NVIDIA's, ten on RDNA's 100 MHz counter. Thirty-two bits
        /// is four seconds at the faster, and a hold is milliseconds.
        uint mTicks;
    };

#ifdef RTX_HOST
    static_assert(sizeof(StressConstants) == 4, "StressConstants must be scalar-packed on every side");
}
#endif

#endif
