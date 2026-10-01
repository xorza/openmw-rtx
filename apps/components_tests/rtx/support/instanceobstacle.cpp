#include "instanceobstacle.hpp"

#include <span>
#include <string>

#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/requirements.hpp>

namespace Rtx::Testing
{
    std::string findInstanceObstacle()
    {
        if (Instance::getLoaderVersion() < sApiVersion)
            return "the Vulkan loader is absent or older than this renderer requires";

        try
        {
            const Instance probe{ ValidationOptions{}, std::span<const char* const>{} };
        }
        catch (const Unsupported& obstacle)
        {
            return std::string("no Vulkan driver is installed: ") + obstacle.what();
        }

        return {};
    }
}
