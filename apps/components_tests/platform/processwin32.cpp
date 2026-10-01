#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **A word is one word to `cmd`, a space and a quote in it included**: in double quotes, which
    /// are the only quotes it has. Spelt as `cmd` reads it, and read by `cmd`, which compares the word
    /// with the text written again.
    TEST(RtxPlatformProcessTest, aShellWordIsOneWordWhateverIsInIt)
    {
        const std::string word = Platform::Process::shellWord("it's here");
        EXPECT_EQ(word, "\"it's here\"");
        EXPECT_TRUE(Platform::Process::runShell("if " + word + "==\"it's here\" (exit 0) else (exit 1)").succeeded());
    }

    /// **How a command ended, as `cmd` ran it**: the exit code whole, and no signal, which Windows
    /// has none of.
    TEST(RtxPlatformProcessTest, aCommandEndsWithItsExitCode)
    {
        const Platform::Process::CommandEnd exited = Platform::Process::runShell("exit 3");
        EXPECT_EQ(exited.mExitCode, 3u);
        EXPECT_EQ(exited.mSignal, 0);
        EXPECT_FALSE(exited.succeeded());
        EXPECT_EQ(exited.describe(), "exit code 3");

        EXPECT_TRUE(Platform::Process::runShell("exit 0").succeeded());
    }

    /// **A process runs until it ends, and an id no process has is no process**: this one, then a
    /// PowerShell that wrote its own id and exited — `cmd` has no way to say its own — then nought.
    TEST(RtxPlatformProcessTest, aProcessIsRunningUntilItEnds)
    {
        EXPECT_TRUE(Platform::Process::isRunning(Platform::Process::currentId()));

        const std::filesystem::path written = TestingOpenMW::outputFilePath("ended-shell-id");
        ASSERT_TRUE(
            Platform::Process::runShell("powershell -NoProfile -NonInteractive -Command \"Set-Content "
                                        "-Encoding ascii -LiteralPath '"
                + Files::pathToUnicodeString(written) + "' -Value $PID\"")
                .succeeded());
        std::uint32_t ended = 0;
        std::ifstream(written) >> ended;
        ASSERT_NE(ended, 0u) << "the shell wrote no id";
        EXPECT_FALSE(Platform::Process::isRunning(ended));

        EXPECT_FALSE(Platform::Process::isRunning(0));
    }
}
