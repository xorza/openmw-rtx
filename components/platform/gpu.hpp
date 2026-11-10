#pragma once

#include <cstdint>
#include <vector>

/// What the system's display adapters are, as the launcher needs it before a Vulkan device exists:
/// whether DLSS can be offered at all. One header over `gpu.cpp`, `gpuwin32.cpp` and `gpulinux.cpp`.
namespace Platform::Gpu
{
    /// Who made a card, the one list of the vendors anything here tells apart.
    enum class Vendor : std::uint8_t
    {
        Nvidia,
        Amd,
        Intel,
        Other,
    };

    /// The vendor a PCI vendor id names, as Vulkan's `vendorID`, DXGI's `VendorId` and sysfs state it.
    constexpr Vendor vendorOf(const std::uint32_t pciId)
    {
        switch (pciId)
        {
            case 0x10DE:
                return Vendor::Nvidia;
            case 0x1002:
                return Vendor::Amd;
            case 0x8086:
                return Vendor::Intel;
            default:
                return Vendor::Other;
        }
    }

    /// The vendors of the display adapters the system names, each once. Empty where the system does
    /// not say, which is not the same as a machine without a card.
    std::vector<Vendor> vendors();

    /// Whether any display adapter is `vendor`'s.
    bool has(Vendor vendor);
}
