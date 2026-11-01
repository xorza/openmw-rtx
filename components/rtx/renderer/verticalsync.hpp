#pragma once

namespace Rtx
{
    /// How a present meets the monitor's refresh, in the core's own words: a host's setting is its
    /// window layer's, which the host turns into this.
    enum class VerticalSync
    {
        /// Shown once drawn, the newest frame in place of one still waiting where the surface can.
        Off,

        /// Shown at a refresh, and a frame that misses one waits for the next.
        On,

        /// Shown at a refresh, and a frame that misses one is shown at once, torn.
        Adaptive,
    };
}
