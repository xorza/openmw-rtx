#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

#include <gtest/gtest.h>

// NOLINTNEXTLINE(modernize-deprecated-headers): POSIX's `SIGKILL`, which <csignal> need not define
#include <signal.h> // IWYU pragma: keep: `SIGKILL`, which glibc defines in a bits header nothing maps to this one
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <components/platform/kernelfilelinux.hpp>
#include <components/platform/linuxtext.hpp>
#include <components/platform/processhold.hpp>
#include <components/platform/processholdlinux.hpp>

namespace
{
    /// How a test holds a process: the way the kernel gives, a pidfd here, or by its start time, the way a kernel
    /// older than 5.3 is held.
    enum class Way : std::int32_t
    {
        Given,
        StartTime,
    };

    constexpr std::array sWays{ Way::Given, Way::StartTime };

    const char* nameOf(Way way)
    {
        return way == Way::Given ? "held by the pidfd" : "held by the start time";
    }

    Platform::ProcessHold holdBy(Way way, pid_t id)
    {
        return way == Way::Given ? Platform::ProcessHold(static_cast<std::uint32_t>(id))
                                 : Platform::holdByStartTime(static_cast<std::uint32_t>(id));
    }

    /// The state letter the process `id`'s `stat` states, or nought where it cannot be read.
    char stateOf(pid_t id)
    {
        std::array<char, 1024> buffer{};
        const std::string path = "/proc/" + std::to_string(id) + "/stat";
        const std::optional<std::string_view> text = Platform::KernelFile::read(path.c_str(), buffer);
        if (!text.has_value())
            return 0;
        const std::optional<Platform::LinuxText::ProcessStat> stat = Platform::LinuxText::processStat(*text);
        return stat.has_value() ? stat->mState : 0;
    }

    /// A child that waits on a pipe this process holds, so it ends with this process wherever a check stops a test
    /// early, and ends of itself once the pipe's other end, `mWriteEnd`, is written to or closed.
    struct Waiting
    {
        pid_t mChild = -1;
        std::int32_t mWriteEnd = -1;
    };

    Waiting startWaiting()
    {
        std::array<std::int32_t, 2> pipeEnds{};
        if (pipe(pipeEnds.data()) != 0)
            return Waiting{};
        const pid_t child = fork();
        if (child == 0)
        {
            close(pipeEnds[1]);
            char byte = 0;
            std::ignore = read(pipeEnds[0], &byte, 1);
            _exit(0);
        }
        close(pipeEnds[0]);
        return Waiting{ .mChild = child, .mWriteEnd = pipeEnds[1] };
    }

    /// **A held process is ended, waited for, and gone once reaped**, held either way: standing, it has not ended,
    /// and a wait bounded short of its end says so; ended, the wait sees it, a second end finds it gone while it is a
    /// zombie, which a signal is taken by, and the parent collects `SIGKILL`. Once reaped the hold still says it
    /// ended, and an end finds it gone, where `kill` would reach whatever took the id. A hold taken after the reap
    /// holds nothing: it has ended, and nothing can be ended through it. A start time holds no descriptor.
    TEST(PlatformProcessHoldTest, aHeldProcessIsEndedWaitedForAndGoneOnceReaped)
    {
        for (const Way way : sWays)
        {
            SCOPED_TRACE(nameOf(way));
            const Waiting waiting = startWaiting();
            ASSERT_GT(waiting.mChild, 0);

            const Platform::ProcessHold held = holdBy(way, waiting.mChild);
            ASSERT_TRUE(held.isOpen());
            EXPECT_EQ(held.id(), static_cast<std::uint32_t>(waiting.mChild));
            EXPECT_EQ(held.native() == -1, way == Way::StartTime);
            EXPECT_FALSE(held.hasEnded());
            EXPECT_FALSE(held.waitForEnd(std::chrono::milliseconds(20)));

            EXPECT_EQ(held.end(), Platform::ProcessEnd::Ended);
            EXPECT_TRUE(held.waitForEnd(std::chrono::seconds(10)));
            EXPECT_EQ(stateOf(waiting.mChild), 'Z') << "ended and not yet reaped";
            EXPECT_EQ(held.end(), Platform::ProcessEnd::Gone);
            std::int32_t status = 0;
            ASSERT_EQ(waitpid(waiting.mChild, &status, 0), waiting.mChild);
            EXPECT_TRUE(WIFSIGNALED(status));
            EXPECT_EQ(WTERMSIG(status), SIGKILL);

            EXPECT_TRUE(held.hasEnded());
            EXPECT_EQ(held.end(), Platform::ProcessEnd::Gone);
            close(waiting.mWriteEnd);

            const Platform::ProcessHold late = holdBy(way, waiting.mChild);
            EXPECT_FALSE(late.isOpen());
            EXPECT_EQ(late.native(), -1);
            EXPECT_TRUE(late.hasEnded());
            EXPECT_EQ(late.end(), Platform::ProcessEnd::Failed);
        }
    }

    /// **A wait bounded by the longest a duration holds waits for the end**, either way, and does not overflow into
    /// a bound already past: the child ends of itself once this process closes the pipe it reads, 20 ms on.
    TEST(PlatformProcessHoldTest, aWaitAsLongAsADurationHoldsWaitsForTheEnd)
    {
        for (const Way way : sWays)
        {
            SCOPED_TRACE(nameOf(way));
            const Waiting waiting = startWaiting();
            ASSERT_GT(waiting.mChild, 0);

            const Platform::ProcessHold held = holdBy(way, waiting.mChild);
            ASSERT_TRUE(held.isOpen());
            std::jthread closer([end = waiting.mWriteEnd] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                close(end);
            });
            EXPECT_TRUE(held.waitForEnd(std::chrono::milliseconds::max()));
            std::int32_t status = 0;
            ASSERT_EQ(waitpid(waiting.mChild, &status, 0), waiting.mChild);
            EXPECT_TRUE(WIFEXITED(status));
            EXPECT_EQ(WEXITSTATUS(status), 0);
        }
    }

    /// What `aProcessEndsWithItsLastThreadNotItsFirst`'s child runs, holding its grandchild `way`: a check that fails
    /// ends it with its own number.
    [[noreturn]] void endWithTheLastThread(Way way)
    {
        std::array<std::int32_t, 2> pipeEnds{};
        if (pipe(pipeEnds.data()) != 0)
            std::_Exit(1);
        const pid_t grandchild = fork();
        if (grandchild == -1)
            std::_Exit(1);
        if (grandchild == 0)
        {
            close(pipeEnds[1]);
            std::thread([end = pipeEnds[0]] {
                char byte = 0;
                std::ignore = read(end, &byte, 1);
                _exit(0);
            }).detach();
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Linux's syscall, which ends this thread alone
            syscall(SYS_exit, 0);
        }
        close(pipeEnds[0]);

        const Platform::ProcessHold held = holdBy(way, grandchild);
        if (!held.isOpen())
            std::_Exit(2);

        for (std::int32_t waited = 0; stateOf(grandchild) != 'Z'; ++waited)
        {
            if (waited == 1000)
                std::_Exit(3);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (held.waitForEnd(std::chrono::milliseconds(50)))
            std::_Exit(4);
        if (write(pipeEnds[1], "x", 1) != 1)
            std::_Exit(5);
        if (!held.waitForEnd(std::chrono::seconds(10)))
            std::_Exit(6);
        if (waitpid(grandchild, nullptr, 0) != grandchild)
            std::_Exit(7);
        std::_Exit(0);
    }

    /// **A process ends with its last thread, not its first**, either way: a grandchild's first thread ends alone,
    /// and the hold waits out its bound while the other stands on a pipe; written to, that one ends, and the hold
    /// sees the process end. Seen ended only once its first thread reads as a zombie, so the first check is not a
    /// race the thread has not run yet. In a child the binary starts afresh, which has the one thread a fork may copy
    /// and then start another from (`main`'s death test style).
    TEST(PlatformProcessHoldTest, aProcessEndsWithItsLastThreadNotItsFirst)
    {
        EXPECT_EXIT(endWithTheLastThread(Way::Given), ::testing::ExitedWithCode(0), "") << nameOf(Way::Given);
        EXPECT_EXIT(endWithTheLastThread(Way::StartTime), ::testing::ExitedWithCode(0), "") << nameOf(Way::StartTime);
    }
}
