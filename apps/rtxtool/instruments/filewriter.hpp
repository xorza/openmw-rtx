#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace RtxTool
{
    /// **A run's files written on a thread of its own, in the order they were handed over.** A tour writes its record
    /// and its findings after every leg, so one that dies says how far it came; written on the frame's thread, the
    /// write stood in the first frame of the leg after, which the measurement left out for it.
    class FileWriter
    {
    public:
        /// How a text becomes its file.
        enum class As : std::uint8_t
        {
            /// The whole of the file, in place of whatever stood there (`writeReport`).
            Whole,

            /// Added at the end of the file a `Whole` write began.
            Appended,
        };

        /// The most writes that wait at once: a hand-over past it waits for the thread, on a disk that stopped.
        static constexpr std::size_t sPendingMost = 16;

        /// The most failures kept for `finish` to answer: a disk that refuses every write says so eight times.
        static constexpr std::size_t sFailuresMost = 8;

        FileWriter();

        /// Writes what was handed over, then ends the thread.
        ~FileWriter() = default;

        FileWriter(const FileWriter&) = delete;
        FileWriter& operator=(const FileWriter&) = delete;

        /// Hands `text` over to be written to `path`.
        void write(std::filesystem::path path, std::string text, As as);

        /// Waits until everything handed over is written, and answers each write that failed since the last ask,
        /// by its file and the system's reason.
        std::vector<std::string> finish();

    private:
        struct Job
        {
            std::filesystem::path mPath;
            std::string mText;
            As mAs = As::Whole;
        };

        void serve(const std::stop_token& stop);

        std::mutex mMutex;
        std::condition_variable_any mChanged;

        /// What waits, the oldest first: `sPendingMost` at most.
        std::vector<Job> mPending;
        bool mWriting = false;
        std::vector<std::string> mFailures;

        /// Last, so it ends before what it reads does.
        std::jthread mWorker;
    };
}
