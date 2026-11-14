#pragma once

#include <chrono>
#include <filesystem>
#include <string_view>

#include <components/platform/file.hpp>

namespace RtxTool
{
    /// perf's control fifo, so a recording holds the frames that were measured and nothing else.
    ///
    /// `perf record --delay=-1 --control=fifo:<path>` starts with its counters off and turns them on when
    /// the word `enable` arrives on the fifo. Handed that path, this writes the word around each place's
    /// measured frames, so the profile is those frames alone. Trimming a whole-run recording to a guessed
    /// boundary instead reports a quarter of its samples against code the benchmark never claimed to measure.
    ///
    /// An empty path does nothing at all, which is every run that is not being profiled.
    class PerfControl
    {
    public:
        /// How long `open` waits for perf to open its end: perf attaches to a running process and
        /// loads a BPF program for `--offcpu` before it reads, which takes seconds and not minutes.
        static constexpr std::chrono::milliseconds sReaderWait{ 30000 };

        explicit PerfControl(std::filesystem::path fifo, std::chrono::milliseconds readerWait = sReaderWait);

        /// Opens the fifo, where it is not open yet: ahead of a place, so no measured frame pays it.
        ///
        /// Not on construction, because the reader has to be there first: opening a fifo for writing with
        /// nobody reading fails without blocking, and perf attaches only after the harness started. The open
        /// retries until the reader is there, and throws once the wait is spent.
        void open();

        /// Starts counting, on the first measured frame: one write, the fifo `open` opened.
        void enable();

        /// Stops counting. Silent before the first `enable`, so a run stopped early is not an error.
        void disable();

    private:
        void send(std::string_view command);

        std::filesystem::path mFifo;
        std::chrono::milliseconds mReaderWait;
        Platform::File::ScopedHandle mHandle;
    };
}
