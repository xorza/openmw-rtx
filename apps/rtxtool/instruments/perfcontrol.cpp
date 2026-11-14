#include "perfcontrol.hpp"

#include <chrono>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <components/crashcatcher/crashinstall.hpp>
#include <components/platform/fifo.hpp>

namespace RtxTool
{
    PerfControl::PerfControl(std::filesystem::path fifo, const std::chrono::milliseconds readerWait)
        : mFifo(std::move(fifo))
        , mReaderWait(readerWait)
    {
    }

    void PerfControl::open()
    {
        if (mFifo.empty() || mHandle != Platform::File::Handle::Invalid)
            return;

        // A wait the run asked for, inside a frame, which the hang watch is told is not a hang:
        // perf's thirty seconds are past the twenty the shipped watch allows a frame.
        const Crash::HangPause waiting;

        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + mReaderWait;
        while (true)
        {
            try
            {
                mHandle = Platform::Fifo::openForWriting(mFifo);
                return;
            }
            catch (const std::system_error& error)
            {
                // `ENXIO` is "nobody reads it yet"; every other failure is final.
                if (error.code() != std::errc::no_such_device_or_address
                    || std::chrono::steady_clock::now() >= deadline)
                    throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void PerfControl::enable()
    {
        open();
        if (mHandle == Platform::File::Handle::Invalid)
            return;

        send("enable\n");
    }

    void PerfControl::disable()
    {
        if (mHandle == Platform::File::Handle::Invalid)
            return;

        send("disable\n");
    }

    void PerfControl::send(std::string_view command)
    {
        Platform::Fifo::write(mHandle, command.data(), command.size());
    }
}
