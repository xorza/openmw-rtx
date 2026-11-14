#pragma once

#include <cstdint>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// The names a driver reports a pipeline's use under (`CompiledUse`), empty where it reports none, and how
    /// its scratch figure is read.
    struct StatisticNames
    {
        std::string_view mScratch;
        std::string_view mRegisters;
        std::string_view mShared;

        /// The bits of the scratch statistic that are bytes: NVIDIA's `Local Memory Size` has read
        /// as 2^36 above the bytes spilled, and as the bytes alone under driver 615.78 on Linux, so
        /// its low 32 bits are the bytes either way; every other driver's is the bytes.
        std::uint64_t mScratchBits = ~std::uint64_t{ 0 };

        /// The most scratch a lane holds on the driver at no cost a bench showed, in bytes: a summary warns
        /// of a pipeline that holds more (`PipelineTally`). NVIDIA's compiler keeps what a kernel's registers
        /// do not hold in shared memory and what is over in a lane's local memory, its scratch, and 112 bytes
        /// of it taken off a stage moved its time by nothing
        /// (`docs/research/2026-11-14-nvidia-register-spills.md`); on every other driver scratch is a spill
        /// from its first byte.
        std::uint64_t mScratchFreeBytes = 0;
    };

    /// What differs by the vendor and the driver of a device, read once off its identity: the one place
    /// a vendor or a driver is told apart (`AGENTS.md`, "GPU vendors stand behind one entity"). Every
    /// other file reads a capability here and names no vendor.
    struct DriverProfile
    {
        /// Whether NVIDIA's NGX, which DLSS runs in, runs on the device: an NVIDIA card's.
        bool mNgx = false;

        StatisticNames mStatistics{};

        /// Whether the driver's compiler takes `MaximallyReconvergesKHR` on a module that traces ray queries,
        /// as the pinning declares it (`pinModule`). AMD's Windows driver 26.8.1 reads a null pointer compiling one
        /// (`bounce.comp`, `visibility.comp`; the module without ray queries compile), so it is handed those
        /// modules without the mode, and its lanes reconverge where its compiler happens to put them.
        bool mReconvergesRayQueries = true;

        /// What a device of `vendorId` running `driver` comes to.
        static DriverProfile of(std::uint32_t vendorId, VkDriverId driver);
    };
}
