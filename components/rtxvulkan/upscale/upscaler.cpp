#include "upscaler.hpp"

#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/upscalerextensions.hpp>

namespace Rtx
{
    std::unique_ptr<Upscaler> makeUpscaler(const Device&, VkInstance)
    {
        // Named rather than quietly ignored. A renderer that cannot upscale and renders at the
        // output size anyway is one whose frame times mean something else entirely.
        throw Unsupported("upscaling was asked for and this renderer has no upscaler");
    }

    std::string describeUpscaling(const Device&, VkInstance)
    {
        return "none";
    }

    std::span<const char* const> upscalerInstanceExtensions()
    {
        return {};
    }

    std::span<const char* const> upscalerDeviceExtensions()
    {
        return {};
    }
}
