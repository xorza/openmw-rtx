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

    /// What every call answers in a build without the backend: the constructor refuses, so an
    /// explicit `dlss` fails at start, naming why, and `auto` falls back to FSR.
    DlssUpscaler::DlssUpscaler(const Device& device, const Dlss::Application*)
        : mDevice(device)
    {
        throw UpscalerRefused("this build was made without NVIDIA DLSS support");
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

    void DlssUpscaler::resize(Upscale, VkExtent2D, VkExtent2D, const ArenaRun&) {}

    void DlssUpscaler::release() {}

    bool DlssUpscaler::isAt(VkExtent2D, VkExtent2D) const
    {
        return false;
    }

    HandedImage DlssUpscaler::record(VkCommandBuffer, Uploads&, const UpscaleInputs& inputs, GpuTimer*)
    {
        return inputs.mColour;
    }

    std::string_view DlssUpscaler::describe()
    {
        return "DLSS (not built)";
    }
}
