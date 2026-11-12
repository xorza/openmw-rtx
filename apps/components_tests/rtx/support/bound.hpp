#pragma once

#include <chrono>

namespace Rtx::Testing
{
    /// **The bound every waiting loop of a test holds to**: past it the loop fails naming what it
    /// waited for, where a loop with none hung to CTest's timeout and said nothing. Started when made.
    class WaitBound
    {
    public:
        explicit WaitBound(std::chrono::steady_clock::duration patience)
            : mDue(std::chrono::steady_clock::now() + patience)
        {
        }

        /// Whether the bound has passed.
        bool passed() const { return std::chrono::steady_clock::now() >= mDue; }

    private:
        std::chrono::steady_clock::time_point mDue;
    };

    /// How long a test waits for kernels or content a cold cache makes, the slowest of which took
    /// seconds: past this nothing is coming.
    inline constexpr std::chrono::minutes sColdPatience{ 5 };

    /// How long a test waits for another thread to reach a point it is about to reach.
    inline constexpr std::chrono::seconds sHandOverPatience{ 10 };
}
