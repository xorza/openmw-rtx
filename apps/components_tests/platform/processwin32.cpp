#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **The running file, and not a name a shell was given**: this binary itself, which stands on
    /// the disk where the system says.
    TEST(RtxPlatformProcessTest, theExecutableIsTheRunningFile)
    {
        const std::optional<std::filesystem::path> self = Platform::Process::executable();
        ASSERT_TRUE(self.has_value());
        EXPECT_EQ(self->filename(), "components-tests.exe");
        EXPECT_TRUE(self->is_absolute());
        EXPECT_TRUE(std::filesystem::is_regular_file(*self));
    }

    /// **A word is one word to `cmd`, a space and a quote in it included**: in double quotes, which
    /// are the only quotes it has. Spelt as `cmd` reads it, and read by `cmd`, which compares the word
    /// with the text written again.
    TEST(RtxPlatformProcessTest, aShellWordIsOneWordWhateverIsInIt)
    {
        const std::string word = Platform::Process::shellWord("it's here");
        EXPECT_EQ(word, "\"it's here\"");
        EXPECT_TRUE(Platform::Process::runShell("if " + word + "==\"it's here\" (exit 0) else (exit 1)").succeeded());

        // **And a word `cmd` cannot keep is refused**: `%USERNAME%` inside the quotes is a name,
        // and a quote ends the word. A lone `%`, a frame pattern's, names nothing.
        EXPECT_THROW(Platform::Process::shellWord("C:\\%USERNAME%\\frames"), std::invalid_argument);
        EXPECT_THROW(Platform::Process::shellWord("a \"quoted\" folder"), std::invalid_argument);
        EXPECT_EQ(Platform::Process::shellWord("frames\\%05d.png"), "\"frames\\%05d.png\"");
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
