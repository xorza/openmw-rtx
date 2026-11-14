#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <components/platform/library.hpp>
#include <components/platform/uniquehold.hpp>
#include <components/rtx/common/cardaddress.hpp>

#include "cardsample.hpp"
#include "gpuclock.hpp"

namespace RtxTool
{
    /// NVIDIA's management library (what `nvidia-smi` reads), opened by name at run time and never linked, because
    /// only one vendor's driver ships it and a build must start on any card; a box without it answers no clock and
    /// no card line. Asked directly, not through the tool: forking `nvidia-smi` costs tens of milliseconds a time, and
    /// the driver's process samples come five times a second and are lost between slower polls. The card is found by
    /// PCI address, never NVML's index, which numbers cards in an order of its own.
    class Nvml
    {
    public:
        /// Opens the library and the card at `card`.
        explicit Nvml(const Rtx::PciAddress& card);
        Nvml(const Nvml&) = delete;
        Nvml& operator=(const Nvml&) = delete;
        Nvml(Nvml&&) = delete;
        Nvml& operator=(Nvml&&) = delete;
        ~Nvml() = default;

        bool isOpen() const { return mDevice != nullptr; }

        /// Whether the driver's library loaded, which a driver may ship without, whether or not it
        /// then opened the card.
        bool isLoaded() const { return mLibrary.isOpen(); }

        /// Why the card cannot be asked, or empty where it can.
        std::string_view describeAbsence() const { return mAbsence; }

        /// Whether the driver's process samples can be read, and if not why: the library's own
        /// absence where it is not open, the device's want of samples where it is.
        bool hasSamples() const { return mProcessUtilization != nullptr; }
        std::string_view describeUnsampled() const { return isOpen() ? mUnsampled : mAbsence; }

        /// One reading of the card's clocks, its temperature and what holds its clock back, or
        /// one that answered nothing where the library is not open or the driver refused.
        GpuClock readClock() const;

        /// Every process sample the driver took since the last call, into `into`, which is cleared first. The
        /// samples held when this was opened are where counting starts, so a process that last drew a minute before
        /// the run is not read as drawing in it. The driver keeps one sample a process, so slow polling loses some.
        void readSamples(std::vector<CardSample>& into);

        /// What the process the driver numbers `pid` is called — its executable's own name without
        /// the directory, its extension kept where the system gives one — into `into`; `pid <n>`
        /// where the process is gone.
        void nameProcess(std::uint32_t pid, std::string& into) const;

        /// The executable's name in what the driver calls a process: what stands before the first option, without
        /// its directory. The driver hands back `argv[0]`, which a browser fills with its whole command line; not
        /// the first word, because a directory may hold a space.
        static std::string_view executableOf(std::string_view called);

        /// `card` as NVML spells a bus id: `00000000:01:00.0`.
        static std::string busId(const Rtx::PciAddress& card);

    private:
        /// `nvmlProcessUtilizationSample_t`, laid out as the driver writes it.
        struct RawSample
        {
            std::uint32_t mPid;
            std::uint64_t mStamp;
            std::uint32_t mSm;
            std::uint32_t mMemory;
            std::uint32_t mEncoder;
            std::uint32_t mDecoder;
        };

        using Device = void*;
        using Return = int;

        /// A started library, ended as it goes: held by the shutdown it is ended with, and built
        /// only once `nvmlInit_v2` answered success.
        struct Shutting
        {
            using Handle = Return (*)();
            static constexpr Handle sNone = nullptr;
            static void close(Handle shutdown) noexcept { shutdown(); }
        };
        using Session = Platform::UniqueHold<Shutting>;

        /// Fills `mScratch` with the samples newer than `mCursor`, answering the driver's own
        /// word for how it went.
        Return fetch(unsigned& count);

        Platform::Library::ScopedHandle mLibrary;

        /// After the library, so the library is shut down before it is closed.
        Session mSession;
        Device mDevice = nullptr;
        std::string mAbsence;
        std::string_view mUnsampled;

        Return (*mInit)() = nullptr;
        Return (*mShutdown)() = nullptr;
        Return (*mHandleByBusId)(const char*, Device*) = nullptr;
        Return (*mClockInfo)(Device, int, unsigned*) = nullptr;
        Return (*mTemperature)(Device, int, unsigned*) = nullptr;
        Return (*mEventReasons)(Device, unsigned long long*) = nullptr;
        Return (*mProcessUtilization)(Device, RawSample*, unsigned*, unsigned long long) = nullptr;
        Return (*mProcessName)(unsigned, char*, unsigned) = nullptr;

        /// The driver's clock at the newest sample read, which the next read counts from.
        std::uint64_t mCursor = 0;

        /// Room for the driver's answer, grown to what it asked for the once it asked.
        std::vector<RawSample> mScratch;
    };
}
