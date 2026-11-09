#include "dlss.hpp"

namespace Rtx::Dlss
{
    Application::Application(const Platform::Dlss::Folders&) {}

    std::vector<std::string> Application::requiredInstanceExtensions() const
    {
        return {};
    }

    std::vector<std::string> Application::requiredDeviceExtensions(VkInstance, VkPhysicalDevice) const
    {
        return {};
    }
}
