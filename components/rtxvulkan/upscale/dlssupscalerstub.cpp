#include "dlssupscaler.hpp"

#include <string_view>

#include <components/rtx/common/error.hpp>

namespace Rtx
{
    struct DlssUpscaler::Context
    {
    };

    struct DlssUpscaler::Targets
    {
    };

    DlssUpscaler::DlssUpscaler(const Device& device)
        : mDevice(device)
    {
    }

    DlssUpscaler::~DlssUpscaler() = default;

    void DlssUpscaler::declareShared(VkExtent2D, VkExtent2D, std::vector<ArenaImage>&) {}

    std::size_t DlssUpscaler::getSharedCount()
    {
        return 0;
    }

    VkDeviceSize DlssUpscaler::bytesAt(const Device&, VkExtent2D, VkExtent2D)
    {
        return 0;
    }

    void DlssUpscaler::resize(Upscale, VkExtent2D, VkExtent2D, const ArenaRun&)
    {
        throw Unsupported("this build was made without NVIDIA DLSS support");
    }

    void DlssUpscaler::release() {}

    bool DlssUpscaler::isAt(VkExtent2D, VkExtent2D) const
    {
        return false;
    }

    HandedImage DlssUpscaler::record(VkCommandBuffer, const UpscaleInputs&, GpuTimer*)
    {
        throw Unsupported("this build was made without NVIDIA DLSS support");
    }

    std::string_view DlssUpscaler::describe()
    {
        return "DLSS (not built)";
    }
}
