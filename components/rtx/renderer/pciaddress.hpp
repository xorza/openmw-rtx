#pragma once

#include <cstdint>

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

        bool operator==(const PciAddress&) const = default;
    };
}
