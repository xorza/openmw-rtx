#pragma once

#include <chrono>
#include <ratio>

namespace Rtx
{
    /// Milliseconds between two readings of the steady clock, which is what every timed figure in
    /// this fork is. Apart from the figures, because a pass timing its own run and a backend timing
    /// a wait need the subtraction and none of the frame's rows.
    inline double since(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }
}
