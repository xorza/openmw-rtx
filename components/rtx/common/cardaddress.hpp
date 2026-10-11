#pragma once

#include <cstdint>
#include <optional>

namespace Rtx
{
    /// Where a card stands on the PCI bus: the one name its graphics driver, NVML and sysfs share.
    struct PciAddress
    {
        std::uint32_t mDomain = 0;
        std::uint32_t mBus = 0;
        std::uint32_t mDevice = 0;
        std::uint32_t mFunction = 0;

        bool operator==(const PciAddress&) const = default;
    };

    /// Which card a renderer runs on.
    struct CardAddress
    {
        /// The PCI vendor's number: 0x10de is NVIDIA's, 0x1002 AMD's.
        std::uint32_t mVendor = 0;

        /// Nothing where the driver does not say.
        std::optional<PciAddress> mPci;
    };
}
