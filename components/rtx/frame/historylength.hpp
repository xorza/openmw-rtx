#pragma once

#include <cstdint>

#include <components/rtx/shaders/look.h>

namespace Rtx
{
    /// How many frames the bounce's history keeps, from a span of time (`Shaders::ACCUMULATE_SECONDS`): NRD's
    /// `GetMaxAccumulatedFrameNum`, over the frame time NRD smooths for it — a low pass of 0.2 seconds, so a
    /// stutter of a frame or two does not cut every history short and leave it to grow back.
    class HistoryLength
    {
    public:
        /// Takes a frame traced `sinceLast` seconds after the one before, nought where nothing was traced before
        /// it or no clock measured it, and returns how many frames a history `seconds` long keeps: at least one.
        float advance(float sinceLast, float seconds);

    private:
        /// The smoothed frame time, at `ACCUMULATE_REFERENCE_RATE` until a clock measured one.
        float mFrameSeconds = 1.0f / Shaders::ACCUMULATE_REFERENCE_RATE;
    };
}
