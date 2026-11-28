#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <tuple>

#include <gtest/gtest.h>

// NOLINTNEXTLINE(modernize-deprecated-headers): POSIX's `SIGKILL`, which <csignal> need not define
#include <signal.h> // IWYU pragma: keep: `SIGKILL`, which glibc defines in a bits header nothing maps to this one
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <components/platform/processhold.hpp>
#include <components/platform/wholefile.hpp>

namespace
{
    /// **A held process is ended, waited for, and gone once reaped**: standing, it has not ended, and a wait
    /// bounded short of its end says so; ended, the wait sees it, a second end finds it gone while it is a zombie,
    /// which a signal is taken by, and the parent collects `SIGKILL`. Once reaped the hold still says it ended, and
    /// an end finds it gone, where `kill` would reach whatever took the id.
    /// A hold taken after the reap holds nothing: it has ended, and nothing can be ended through it. The child
    /// waits on a pipe this process holds, so it ends with this process wherever a check stops the test early.
    TEST(PlatformProcessHoldTest, aHeldProcessIsEndedWaitedForAndGoneOnceReaped)
    {
        std::array<std::int32_t, 2> pipeEnds{};
        ASSERT_EQ(pipe(pipeEnds.data()), 0);
        const pid_t child = fork();
        ASSERT_NE(child, -1);
        if (child == 0)
        {
            char byte = 0;
            std::ignore = read(pipeEnds[0], &byte, 1);
            _exit(0);
        }
        close(pipeEnds[0]);

        const Platform::ProcessHold held(static_cast<std::uint32_t>(child));
        ASSERT_TRUE(held.isOpen());
        EXPECT_EQ(held.id(), static_cast<std::uint32_t>(child));
        EXPECT_FALSE(held.hasEnded());
        EXPECT_FALSE(held.waitForEnd(std::chrono::milliseconds(20)));

        EXPECT_EQ(held.end(), Platform::ProcessEnd::Ended);
        EXPECT_TRUE(held.waitForEnd(std::chrono::seconds(10)));
        EXPECT_EQ(held.end(), Platform::ProcessEnd::Gone);
        std::int32_t status = 0;
        ASSERT_EQ(waitpid(child, &status, 0), child);
        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_EQ(WTERMSIG(status), SIGKILL);

        EXPECT_TRUE(held.hasEnded());
        EXPECT_EQ(held.end(), Platform::ProcessEnd::Gone);
        close(pipeEnds[1]);

        const Platform::ProcessHold late(static_cast<std::uint32_t>(child));
        EXPECT_FALSE(late.isOpen());
        EXPECT_EQ(late.native(), -1);
        EXPECT_TRUE(late.hasEnded());
        EXPECT_EQ(late.end(), Platform::ProcessEnd::Failed);
    }

    /// **A wait bounded by the longest a duration holds waits for the end**, and does not overflow into a bound
    /// already past: the child ends of itself once this process closes the pipe it reads, 20 ms on.
    TEST(PlatformProcessHoldTest, aWaitAsLongAsADurationHoldsWaitsForTheEnd)
    {
        std::array<std::int32_t, 2> pipeEnds{};
        ASSERT_EQ(pipe(pipeEnds.data()), 0);
        const pid_t child = fork();
        ASSERT_NE(child, -1);
        if (child == 0)
        {
            close(pipeEnds[1]);
            char byte = 0;
            std::ignore = read(pipeEnds[0], &byte, 1);
            _exit(0);
        }
        close(pipeEnds[0]);

        const Platform::ProcessHold held(static_cast<std::uint32_t>(child));
        ASSERT_TRUE(held.isOpen());
        std::jthread closer([end = pipeEnds[1]] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            close(end);
        });
        EXPECT_TRUE(held.waitForEnd(std::chrono::milliseconds::max()));
        std::int32_t status = 0;
        ASSERT_EQ(waitpid(child, &status, 0), child);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }

    /// What `aProcessEndsWithItsLastThreadNotItsFirst`'s child runs: a check that fails ends it with its own number.
    [[noreturn]] void endWithTheLastThread()
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

        const Platform::ProcessHold held(static_cast<std::uint32_t>(grandchild));
        if (!held.isOpen())
            std::_Exit(2);

        const std::string path = "/proc/" + std::to_string(grandchild) + "/stat";
        const auto firstEnded = [&] {
            std::string stat;
            if (!Platform::readWhole(path, stat, Platform::sUnbounded).isOk())
                return false;
            const std::size_t state = stat.rfind(')') + 2;
            return state > 1 && state < stat.size() && stat[state] == 'Z';
        };
        for (std::int32_t waited = 0; !firstEnded(); ++waited)
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

    /// **A process ends with its last thread, not its first**: a grandchild's first thread ends alone, and the
    /// hold waits out its bound while the other stands on a pipe; written to, that one ends, and the hold sees the
    /// process end. Seen ended only once its first thread reads as a zombie, so the first check is not a race the
    /// thread has not run yet. In a child the binary starts afresh, which has the one thread a fork may copy and
    /// then start another from (`main`'s death test style).
    TEST(PlatformProcessHoldTest, aProcessEndsWithItsLastThreadNotItsFirst)
    {
        EXPECT_EXIT(endWithTheLastThread(), ::testing::ExitedWithCode(0), "");
    }
}
