#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <components/debug/debuglog.hpp>

#include "finding.hpp"

namespace RtxTool
{
    /// The log's warnings and errors as a tour hears them, from whichever thread logs: each distinct line counted once
    /// with the leg and the frame it was first said on, at most `sMost` of them, and what the table found besides
    /// counted as dropped. The frame's thread hands the lines over (`drainInto`), where a place is a leg and a frame:
    /// a line said on another thread names no eye.
    class LogTally
    {
    public:
        static constexpr std::size_t sMost = 512;

        /// From the log's listener, on any thread.
        void hear(Debug::Level level, std::string_view message);

        /// From the frame's thread: where a line said now was said.
        void at(std::string_view leg, std::uint32_t frame);

        /// Moves every line heard into `table`, as an error or a warning.
        void drainInto(FindingTable& table);

    private:
        struct Line
        {
            bool mError = false;
            std::string mText;
            std::string mLeg;
            std::uint32_t mFrame = 0;
            std::uint32_t mCount = 0;
        };

        std::mutex mMutex;
        std::vector<Line> mLines;
        std::string mLeg;
        std::uint32_t mFrame = 0;
        std::uint32_t mDropped = 0;
    };
}
