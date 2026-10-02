#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <apps/rtxtool/instruments/frametimes.hpp>
#include <components/testing/util.hpp>

// What `PerfControl` sends down a fifo, read off the fifo's other end. POSIX's alone, because a
// fifo is: the tests that need none of one stay in `frametimes.cpp`, and Windows refuses the fifo
// by name in `fifowin32.cpp`.
namespace RtxTool
{
    namespace
    {
        /// A fifo with its reading end held open, standing in for the `perf record` that would
        /// normally be on the other side of it.
        class Reader
        {
        public:
            explicit Reader(std::filesystem::path path)
                : mPath(std::move(path))
            {
                std::filesystem::remove(mPath);
                EXPECT_EQ(::mkfifo(mPath.c_str(), 0600), 0);

                // Read-only and non-blocking, which is the one combination that opens a fifo with
                // nobody writing to it yet.
                mHandle = ::open(mPath.c_str(), O_RDONLY | O_NONBLOCK);
                EXPECT_GE(mHandle, 0);
            }

            ~Reader()
            {
                if (mHandle >= 0)
                    ::close(mHandle);
                std::filesystem::remove(mPath);
            }

            const std::filesystem::path& getPath() const { return mPath; }

            /// Everything written so far, which is nothing at all if the writer wrote nothing.
            std::string read() const
            {
                std::array<char, 256> buffer{};
                const ssize_t got = ::read(mHandle, buffer.data(), buffer.size());
                return got > 0 ? std::string(buffer.data(), static_cast<std::size_t>(got)) : std::string();
            }

        private:
            std::filesystem::path mPath;
            int mHandle = -1;
        };

        TEST(RtxPerfControlTest, aBracketedRunSendsPerfTheTwoWordsItListensFor)
        {
            const Reader listening(TestingOpenMW::outputFilePath("perf-control-test"));

            {
                PerfControl control(listening.getPath());
                control.enable();
                control.disable();
            }

            EXPECT_EQ(listening.read(), "enable\ndisable\n");
        }

        TEST(RtxPerfControlTest, twoPlacesEachBracketTheirOwnFrames)
        {
            const Reader listening(TestingOpenMW::outputFilePath("perf-control-pair-test"));

            PerfControl control(listening.getPath());
            control.enable();
            control.disable();
            control.enable();
            control.disable();

            // The gap between them is a cell being loaded, and perf counts nothing across it.
            EXPECT_EQ(listening.read(), "enable\ndisable\nenable\ndisable\n");
        }

        /// **The open waits for perf, and does not guess when it comes.** perf attaches to the
        /// harness after the harness started, and loads a BPF program before it opens its end; a
        /// fifo with no reader refuses a writer at once, and a driver that slept half a second
        /// first raced a slow load. A reader that never comes is a failure once the wait is spent.
        TEST(RtxPerfControlTest, theOpenWaitsForAReaderThatComesLateAndNoLonger)
        {
            const std::filesystem::path path = TestingOpenMW::outputFilePath("perf-control-late-test");
            std::filesystem::remove(path);
            ASSERT_EQ(::mkfifo(path.c_str(), 0600), 0);

            int reader = -1;
            std::thread perf([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                reader = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
            });
            PerfControl control(path, std::chrono::seconds(10));
            control.open();
            perf.join();
            ASSERT_GE(reader, 0);

            control.enable();
            std::array<char, 16> buffer{};
            const ssize_t got = ::read(reader, buffer.data(), buffer.size());
            EXPECT_EQ(std::string(buffer.data(), static_cast<std::size_t>(std::max<ssize_t>(got, 0))), "enable\n");
            ::close(reader);

            const auto begun = std::chrono::steady_clock::now();
            PerfControl alone(path, std::chrono::milliseconds(50));
            try
            {
                alone.open();
                ADD_FAILURE() << "a fifo nobody reads was opened";
            }
            catch (const std::system_error& error)
            {
                EXPECT_EQ(error.code(), std::errc::no_such_device_or_address) << error.what();
            }
            EXPECT_GE(std::chrono::steady_clock::now() - begun, std::chrono::milliseconds(50)) << "it did not wait";
            std::filesystem::remove(path);
        }

        TEST(RtxPerfControlTest, aStopBeforeTheFirstFrameSaysNothing)
        {
            const Reader listening(TestingOpenMW::outputFilePath("perf-control-stop-test"));

            PerfControl control(listening.getPath());
            control.disable();

            EXPECT_EQ(listening.read(), "");
        }
    }
}
