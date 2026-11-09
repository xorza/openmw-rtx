#pragma once

#include <cstdint>
#include <vector>

/// What the system's display adapters are, as the launcher needs it before a Vulkan device exists:
/// whether DLSS can be offered at all. One header over `gpuwin32.cpp` and `gpuposix.cpp`.
namespace Platform::Gpu
{
    inline constexpr std::uint32_t sNvidia = 0x10DE;
    inline constexpr std::uint32_t sAmd = 0x1002;
    inline constexpr std::uint32_t sIntel = 0x8086;

    /// The vendor ids of the display adapters the system names, each once. Empty where the system
    /// does not say, which is not the same as a machine without a card.
    std::vector<std::uint32_t> vendors();

    /// Whether any display adapter has `vendor`.
    bool has(std::uint32_t vendor);
}
