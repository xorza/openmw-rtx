#pragma once

#include <span>

namespace Rtx
{
    /// What the upscaler's runtime needs enabled on the instance and on the device, asked before
    /// either exists. Empty in a build without one. The device's list is what `Device` enables
    /// beside the required extensions, so `PhysicalDevice::profileOf` refuses a device that lacks
    /// any of it. Apart from `Upscaler`, because standing the device up needs these two lists and
    /// nothing else of the upscaler, which is built on the device. Defined where the upscaler is:
    /// `dlssupscaler.cpp`, or `noupscaler.cpp` in a build without one.
    std::span<const char* const> upscalerInstanceExtensions();
    std::span<const char* const> upscalerDeviceExtensions();
}
