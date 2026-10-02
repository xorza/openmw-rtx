#pragma once

#include <cstdint>
#include <format>
#include <string>

namespace Rtx
{
    /// Where a device stands on the PCI bus: what names the card that draws to an interface other
    /// than the one it draws through — a driver's management library, the system's own tree of
    /// devices — which each number their cards in an order of their own.
    struct PciAddress
    {
        std::uint32_t mDomain = 0;
        std::uint32_t mBus = 0;
        std::uint32_t mDevice = 0;
        std::uint32_t mFunction = 0;

        /// The address as Linux names a PCI device's directory and `lspci` prints it, in hex:
        /// `0000:03:00.0`.
        std::string describe() const
        {
            return std::format("{:04x}:{:02x}:{:02x}.{:x}", mDomain, mBus, mDevice, mFunction);
        }

        bool operator==(const PciAddress&) const = default;
    };
}
