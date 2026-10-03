#include "threadcounters.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <vector>

// Linux's `perf_event_open`; macOS gives a process no counters of its own threads.
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(__linux__)
namespace RtxTool
{
    namespace
    {
        constexpr std::size_t sEvents = 3;

        /// The processor's own number for one kind of core, where the kernel lists the kinds apart:
        /// a generic event names in its upper half the kind it counts on, and counts nothing on the
        /// other. Nothing where the kernel lists no such kind.
        std::optional<std::uint64_t> kindNumbered(const char* path)
        {
            std::ifstream file(path);
            std::uint64_t type = 0;
            if (file >> type)
                return type;
            return std::nullopt;
        }

        perf_event_attr eventOf(const std::uint64_t config, const bool countKernel)
        {
            perf_event_attr attr{};
            attr.size = sizeof(attr);
            attr.type = PERF_TYPE_HARDWARE;
            attr.config = config;
            attr.exclude_kernel = countKernel ? 0 : 1;
            attr.exclude_hv = 1;
            return attr;
        }

        /// One read of a group, as the kernel lays it out: its count, the time the thread ran with
        /// the group enabled and the time the group counted, and the members' values in the order
        /// they joined.
        struct GroupRead
        {
            std::uint64_t mCount = 0;
            std::uint64_t mEnabled = 0;
            std::uint64_t mRunning = 0;
            std::array<std::uint64_t, sEvents> mValues{};
        };

        /// Cycles, the group's leader, then instructions and cache misses, on one kind of core.
        struct Group
        {
            std::array<int, sEvents> mFds{ -1, -1, -1 };

            /// The kind's number in the events' upper half, nought on a part with one kind.
            std::uint64_t mKind = 0;
            bool mEfficiency = false;
        };
    }

    struct ThreadCounters::Events
    {
        /// One group for a part with one kind of core, and one a kind on a hybrid part: a thread
        /// counts on the group of the kind it is on, and the group's running time is its time there.
        std::vector<Group> mGroups;
        bool mKernelCounted = false;
        bool mTwoKinds = false;
        pid_t mThread = 0;

        /// Why the kernel refused the last event it refused, kept before a close could change it.
        int mRefusal = 0;

        ~Events() { closeAll(); }

        void closeAll()
        {
            for (Group& group : mGroups)
                for (int& fd : group.mFds)
                    if (fd >= 0)
                    {
                        close(fd);
                        fd = -1;
                    }
        }

        /// Opens every group, kernel time counted or not. False where the kernel refuses an event.
        bool open(const bool countKernel)
        {
            for (Group& group : mGroups)
            {
                const std::array attrs{
                    eventOf(group.mKind | PERF_COUNT_HW_CPU_CYCLES, countKernel),
                    eventOf(group.mKind | PERF_COUNT_HW_INSTRUCTIONS, countKernel),
                    eventOf(group.mKind | PERF_COUNT_HW_CACHE_MISSES, countKernel),
                };
                for (std::size_t at = 0; at < sEvents; ++at)
                {
                    perf_event_attr attr = attrs[at];
                    if (at == 0)
                    {
                        attr.disabled = 1;
                        attr.read_format
                            = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
                    }
                    group.mFds[at] = static_cast<int>(syscall(
                        SYS_perf_event_open, &attr, mThread, -1, at == 0 ? -1 : group.mFds[0], PERF_FLAG_FD_CLOEXEC));
                    if (group.mFds[at] < 0)
                    {
                        mRefusal = errno;
                        closeAll();
                        return false;
                    }
                }
            }

            mKernelCounted = countKernel;
            return true;
        }
    };

    ThreadCounters::ThreadCounters()
        : mEvents(std::make_unique<Events>())
    {
        Events& events = *mEvents;
        events.mThread = static_cast<pid_t>(syscall(SYS_gettid));

        const std::optional<std::uint64_t> performance = kindNumbered("/sys/devices/cpu_core/type");
        const std::optional<std::uint64_t> efficiency = kindNumbered("/sys/devices/cpu_atom/type");
        events.mTwoKinds = performance.has_value() && efficiency.has_value();
        if (events.mTwoKinds)
        {
            events.mGroups.push_back(Group{ .mKind = *performance << 32 });
            events.mGroups.push_back(Group{ .mKind = *efficiency << 32, .mEfficiency = true });
        }
        else
            events.mGroups.push_back(Group{});

        // **The kernel's time counted where the kernel allows it**, so the cycles are the clock over
        // the thread's running time, which counts it; without it the clock is not said.
        if (events.open(true) || events.open(false))
            return;

        mWhyNot = events.mRefusal == EACCES || events.mRefusal == EPERM
            ? "the kernel keeps the counters from this user (perf_event_paranoid)"
            : "the processor's counters are not open to this system";
        mEvents.reset();
    }

    ThreadCounters::~ThreadCounters() = default;

    void ThreadCounters::start()
    {
        if (mEvents == nullptr)
            return;

        assert(static_cast<pid_t>(syscall(SYS_gettid)) == mEvents->mThread && "counted on another thread");
        for (const Group& group : mEvents->mGroups)
        {
            ioctl(group.mFds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
            ioctl(group.mFds[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        }
    }

    ThreadCounts ThreadCounters::stop()
    {
        if (mEvents == nullptr)
            return ThreadCounts{ .mWhyNot = mWhyNot };

        for (const Group& group : mEvents->mGroups)
            ioctl(group.mFds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

        ThreadCounts counts{ .mKernelCounted = mEvents->mKernelCounted, .mTwoKinds = mEvents->mTwoKinds };
        std::uint64_t enabled = 0;
        for (const Group& group : mEvents->mGroups)
        {
            GroupRead read{};
            if (::read(group.mFds[0], &read, sizeof(read)) != static_cast<ssize_t>(sizeof(read))
                || read.mCount != sEvents)
                return ThreadCounts{ .mWhyNot = "the kernel's read of the counters came back short" };

            // Every group is enabled for as long as the thread runs, on whichever kind of core, from
            // its own start: the first started is the window.
            enabled = std::max(enabled, read.mEnabled);
            counts.mCycles += read.mValues[0];
            counts.mInstructions += read.mValues[1];
            counts.mCacheMisses += read.mValues[2];
            counts.mRunningNs += read.mRunning;
            if (group.mEfficiency)
                counts.mEfficiencyNs = read.mRunning;
        }
        if (counts.mRunningNs == 0)
            return ThreadCounts{ .mWhyNot = "the processor never counted the thread" };

        // **Below one only where another process counted on the same core**, which shares the
        // counters out in turns; the counts are then scaled up as perf scales its own.
        counts.mCounted = static_cast<double>(counts.mRunningNs) / static_cast<double>(enabled);
        if (counts.mCounted < 1.0)
        {
            const auto scaled = [&](std::uint64_t& value) {
                value = static_cast<std::uint64_t>(static_cast<double>(value) / counts.mCounted);
            };
            scaled(counts.mCycles);
            scaled(counts.mInstructions);
            scaled(counts.mCacheMisses);
        }
        counts.mRead = true;

        return counts;
    }
}
#else
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
#endif
