#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <components/files/conversion.hpp>
#include <components/platform/linuxtext.hpp>
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
        EXPECT_EQ(self->filename(), "components-tests");
        EXPECT_TRUE(self->is_absolute());
        EXPECT_TRUE(std::filesystem::is_regular_file(*self));
    }

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

    /// **A process runs until it ends, and an id no process has is no process**: this one, then a
    /// shell that wrote its own id and exited, then nought and the id past `pid_t`, which `kill`
    /// would read as this process group and as every process there is.
    TEST(RtxPlatformProcessTest, aProcessIsRunningUntilItEnds)
    {
        EXPECT_TRUE(Platform::Process::isRunning(Platform::Process::currentId()));

        const std::filesystem::path written = TestingOpenMW::outputFilePath("ended-shell-id");
        ASSERT_TRUE(Platform::Process::runShell(
            "echo $$ > " + Platform::Process::shellWord(Files::pathToUnicodeString(written)))
                        .succeeded());
        std::uint32_t ended = 0;
        std::ifstream(written) >> ended;
        ASSERT_NE(ended, 0u) << "the shell wrote no id";
        EXPECT_FALSE(Platform::Process::isRunning(ended));

        EXPECT_FALSE(Platform::Process::isRunning(0));
        EXPECT_FALSE(Platform::Process::isRunning(0xFFFFFFFFu));
    }

    /// **Every thread keeps to the performance cores where sysfs lists them apart**, one started
    /// before the call and one after it alike, and nothing is done where it does not list them. Read
    /// back as the kernel states each thread's mask, in its `status`; where there is no such file
    /// to read, as on macOS, the answer alone is checked. In a child, so this binary keeps the
    /// system's choice of cores.
    TEST(RtxPlatformProcessTest, everyThreadKeepsToThePerformanceCoresWhereTheSystemListsThem)
    {
        std::ifstream listed("/sys/devices/cpu_core/cpus");
        const std::optional<std::vector<std::uint32_t>> performance = Platform::LinuxText::parseCpuList(
            std::string{ std::istreambuf_iterator<char>(listed), std::istreambuf_iterator<char>() });

        EXPECT_EXIT(
            {
                std::atomic<bool> done{ false };
                const auto wait = [&] {
                    while (!done.load())
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                };
                std::thread before(wait);
                const std::size_t kept = Platform::Process::keepToPerformanceCores();
                std::thread after(wait);

                int failure = 0;
                if (kept != (performance.has_value() ? performance->size() : 0))
                    failure = 1;

                std::size_t threads = 0;
                std::error_code error;
                for (const auto& task : std::filesystem::directory_iterator("/proc/self/task", error))
                {
                    std::ifstream status(task.path() / "status");
                    for (std::string line; std::getline(status, line);)
                    {
                        constexpr std::string_view key = "Cpus_allowed_list:";
                        if (!line.starts_with(key))
                            continue;
                        ++threads;
                        if (performance.has_value()
                            && Platform::LinuxText::parseCpuList(std::string_view(line).substr(key.size() + 1))
                                != performance)
                        {
                            std::fprintf(stderr, "%s\n", line.c_str());
                            failure = 2;
                        }
                    }
                }
                if (!error && threads < 3)
                    failure = 3;

                done = true;
                before.join();
                after.join();
                std::exit(failure);
            },
            ::testing::ExitedWithCode(0), "");
    }
}
