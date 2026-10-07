#include "threadcounters.hpp"

namespace RtxTool
{
    struct ThreadCounters::Events
    {
    };

    ThreadCounters::ThreadCounters()
        : mWhyNot("this system gives a process no counters of its own threads")
    {
    }

    ThreadCounters::~ThreadCounters() = default;

    void ThreadCounters::start() {}

    ThreadCounts ThreadCounters::stop()
    {
        return ThreadCounts{ .mWhyNot = mWhyNot };
    }
}
