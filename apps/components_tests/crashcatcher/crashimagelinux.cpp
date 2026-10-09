#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <components/crashcatcher/crashimagelinux.hpp>

namespace
{
    using Outcome = Crash::ProcessStat::Outcome;

    /// `text` as a descriptor reads it: the read end of a pipe whose write end is closed.
    int descriptorOf(const std::string_view text)
    {
        int ends[2];
        EXPECT_EQ(pipe(ends), 0);
        EXPECT_EQ(write(ends[1], text.data(), text.size()), static_cast<ssize_t>(text.size()));
        close(ends[1]);
        return ends[0];
    }

    Crash::ProcessStat readText(const std::string_view text)
    {
        const int descriptor = descriptorOf(text);
        const Crash::ProcessStat stat = Crash::readProcessStat(descriptor);
        close(descriptor);
        return stat;
    }

    /// **A process reaped after its `stat` was opened is gone, and not a throw**: the read the
    /// keeper ended on. Two descriptors are opened on the zombie: one read before the reap names
    /// this process as the parent, and the other, read after it, fails with `ESRCH`.
    TEST(CrashImageLinuxTest, aProcessReapedAfterItsStatOpenedIsGone)
    {
        const pid_t child = fork();
        ASSERT_NE(child, -1);
        if (child == 0)
            _exit(0);

        siginfo_t ended{};
        ASSERT_EQ(waitid(P_PID, static_cast<id_t>(child), &ended, WEXITED | WNOWAIT), 0);
        const std::string path = "/proc/" + std::to_string(child) + "/stat";
        const int before = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        const int after = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        ASSERT_NE(before, -1);
        ASSERT_NE(after, -1);

        const Crash::ProcessStat zombie = Crash::readProcessStat(before);
        EXPECT_EQ(zombie.mOutcome, Outcome::Parented);
        EXPECT_EQ(zombie.mParent, getpid());

        ASSERT_EQ(waitpid(child, nullptr, 0), child);
        EXPECT_EQ(Crash::readProcessStat(after).mOutcome, Outcome::Gone);
        close(before);
        close(after);
    }

    /// **The parent follows the last parenthesis**: a name may hold one, and spaces. A text that
    /// stops before the parent, names none, has no name, fills the buffer, or cannot be read at all
    /// is unknown, which the keeper takes for a process of the image.
    TEST(CrashImageLinuxTest, theParentFollowsTheLastParenthesisAndAnythingElseIsUnknown)
    {
        const Crash::ProcessStat named = readText("7 (a) b c) S 42 7 7 0\n");
        EXPECT_EQ(named.mOutcome, Outcome::Parented);
        EXPECT_EQ(named.mParent, 42);

        for (const std::string_view text : { "", "7 (x) S", "7 (x) S y 7", "7 x S 42" })
        {
            SCOPED_TRACE(text);
            EXPECT_EQ(readText(text).mOutcome, Outcome::Unknown);
        }

        // 4095 bytes are read whole, and 4096 fill the buffer: the padding after the parent is
        // what the kernel would write as further fields.
        const std::string head = "7 (x) S 42 ";
        const Crash::ProcessStat longest = readText(head + std::string(4095 - head.size(), '0'));
        EXPECT_EQ(longest.mOutcome, Outcome::Parented);
        EXPECT_EQ(longest.mParent, 42);
        EXPECT_EQ(readText(head + std::string(4096 - head.size(), '0')).mOutcome, Outcome::Unknown);

        const int folder = open(std::filesystem::temp_directory_path().c_str(), O_RDONLY | O_CLOEXEC);
        ASSERT_NE(folder, -1);
        EXPECT_EQ(Crash::readProcessStat(folder).mOutcome, Outcome::Unknown);
        close(folder);
    }
}
