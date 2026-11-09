#include "dlss.hpp"

namespace Rtx::Dlss
{
    std::vector<std::string> requiredInstanceExtensions()
    {
        return {};
    }

    std::vector<std::string> requiredDeviceExtensions(VkInstance, VkPhysicalDevice)
    {
        return {};
    }

    bool isSupported(VkInstance, VkPhysicalDevice)
    {
        return false;
    }
}
