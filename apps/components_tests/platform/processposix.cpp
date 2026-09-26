#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <components/platform/process.hpp>

namespace
{
    /// **A word is one word to the shell, whatever is in it**: a space, a quote — closed, escaped and
    /// opened again — and a dollar sign nothing expands. Spelt as the shell reads it, and read by
    /// the shell: `[` compares the word with the text written another way, so what is checked is
    /// what the shell makes of it and not only its spelling.
    TEST(RtxPlatformProcessTest, aShellWordIsOneWordWhateverIsInIt)
    {
        const std::string word = Platform::Process::shellWord("it's here $HOME");
        EXPECT_EQ(word, "'it'\\''s here $HOME'");
        EXPECT_TRUE(Platform::Process::runShell("[ " + word + " = \"it's here \\$HOME\" ]").succeeded());
    }

    /// **How a command ended, as the shell ran it**: an exit code, and apart from it the signal that
    /// ended a command which did not exit. 9 is `SIGKILL` on every POSIX system.
    TEST(RtxPlatformProcessTest, aCommandEndsWithItsExitCodeOrItsSignal)
    {
        const Platform::Process::CommandEnd exited = Platform::Process::runShell("exit 3");
        EXPECT_EQ(exited.mExitCode, 3u);
        EXPECT_EQ(exited.mSignal, 0);
        EXPECT_FALSE(exited.succeeded());
        EXPECT_EQ(exited.describe(), "exit code 3");

        const Platform::Process::CommandEnd killed = Platform::Process::runShell("kill -KILL $$");
        EXPECT_EQ(killed.mExitCode, 0u);
        EXPECT_EQ(killed.mSignal, 9);
        EXPECT_FALSE(killed.succeeded());
        EXPECT_EQ(killed.describe(), "signal 9");

        EXPECT_TRUE(Platform::Process::runShell("true").succeeded());
    }
}
