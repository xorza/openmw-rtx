#include "upscaler.hpp"

#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>

namespace Rtx
{
    Upscaler::Backend Upscaler::makeBackend(const Device& device, const UpscalerChoice choice,
        DlssAvailability& availability, const Dlss::Application* application)
    {
        // Each backend is made in place and returned as it is, which the backends, pinned to their
        // device, need: a prvalue initialises the member with no move.
        if (chooseUpscaler(choice, availability) == UpscalerKind::Dlss)
        {
            try
            {
                // Said before the backend is made, since it is made in the return: a refusal says
                // otherwise below.
                availability = DlssAvailability::Ready;
                return Backend(std::in_place_type<DlssUpscaler>, device, application);
            }
            catch (const UpscalerRefused& refused)
            {
                if (choice == UpscalerChoice::Dlss)
                    throw;
                availability = DlssAvailability::Refused;
                Log(Debug::Warning) << "NVIDIA DLSS could not start, so FSR is used: " << refused.what();
            }
        }

        return Backend(std::in_place_type<FsrUpscaler>, device);
    }

    Upscaler::Upscaler(const Device& device, const UpscalerChoice choice, const DlssAvailability availability,
        const Dlss::Application* application)
        : mDevice(device)
        , mDlss(availability)
        , mBackend(makeBackend(device, choice, mDlss, application))
    {
    }

    UpscalerKind Upscaler::getKind() const
    {
        return std::holds_alternative<DlssUpscaler>(mBackend) ? UpscalerKind::Dlss : UpscalerKind::Fsr;
    }

    UpscaleMasks Upscaler::getMasks() const
    {
        return getKind() == UpscalerKind::Fsr ? UpscaleMasks::Written : UpscaleMasks::Skipped;
    }

    void Upscaler::resize(const Upscale mode, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        std::visit([&](auto& backend) { backend.resize(mode, render, output, shared); }, mBackend);
    }

    void Upscaler::declareShared(const VkExtent2D render, const VkExtent2D output, std::vector<ArenaImage>& into) const
    {
        std::visit([&](const auto& backend) { std::decay_t<decltype(backend)>::declareShared(render, output, into); },
            mBackend);
    }

    std::size_t Upscaler::getSharedCount() const
    {
        return std::visit(
            [](const auto& backend) { return std::decay_t<decltype(backend)>::getSharedCount(); }, mBackend);
    }

    VkDeviceSize Upscaler::bytesAt(const VkExtent2D render, const VkExtent2D output) const
    {
        return std::visit(
            [&](const auto& backend) { return std::decay_t<decltype(backend)>::bytesAt(mDevice, render, output); },
            mBackend);
    }

    void Upscaler::release()
    {
        std::visit([](auto& backend) { backend.release(); }, mBackend);
    }

    bool Upscaler::isBuilt() const
    {
        return std::visit([](const auto& backend) { return backend.isBuilt(); }, mBackend);
    }

    bool Upscaler::isAt(const VkExtent2D render, const VkExtent2D output) const
    {
        return std::visit([&](const auto& backend) { return backend.isAt(render, output); }, mBackend);
    }

    void Upscaler::reset()
    {
        std::visit([](auto& backend) { backend.reset(); }, mBackend);
    }

    bool Upscaler::isFresh() const
    {
        return std::visit([](const auto& backend) { return backend.isFresh(); }, mBackend);
    }

    HandedImage Upscaler::record(
        const VkCommandBuffer commands, Uploads& uploads, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        return std::visit([&](auto& backend) { return backend.record(commands, uploads, inputs, timer); }, mBackend);
    }

    std::string_view Upscaler::describe() const
    {
        return std::visit([](const auto& backend) { return std::decay_t<decltype(backend)>::describe(); }, mBackend);
    }
}
