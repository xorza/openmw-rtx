#include "testinstance.hpp"

#include <memory>
#include <span>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/requirements.hpp>

namespace Rtx::Testing
{
    Result<std::unique_ptr<Instance>, std::string> makeInstance(const ValidationOptions& options)
    {
        if (Instance::getLoaderVersion() < sApiVersion)
            return Err{ std::string("the Vulkan loader is absent or older than this renderer requires") };

        try
        {
            return std::make_unique<Instance>(options, std::span<const char* const>{});
        }
        catch (const Unsupported& obstacle)
        {
            return Err{ std::string("no Vulkan driver is installed: ") + obstacle.what() };
        }
    }
}
