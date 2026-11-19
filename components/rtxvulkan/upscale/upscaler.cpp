#include "upscaler.hpp"

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "ngx.hpp"

namespace Rtx
{
    Upscaler::Backend Upscaler::makeBackend(const Device& device, const UpscalerChoice choice,
        DlssAvailability& availability, const Dlss::Application* application)
    {
        // Each backend is made in place and returned as it is, which the backends, pinned to their
        // device, need: a prvalue initialises the member with no move.
        switch (choice)
        {
            case UpscalerChoice::Off:
                return Backend(std::in_place_type<NoUpscaler>);
            case UpscalerChoice::Fsr:
                return Backend(std::in_place_type<FsrUpscaler>, device);
            case UpscalerChoice::Dlss:
                break;
        }

        // Said before the backend is made, since it is made in the return: a refusal throws past it.
        availability = DlssAvailability::Ready;
        return Backend(std::in_place_type<DlssUpscaler>, device, application);
    }

    Upscaler::Upscaler(const Device& device, const UpscalerChoice choice, const DlssAvailability availability,
        const Dlss::Application* application)
        : mDlss(availability)
        , mBackend(makeBackend(device, choice, mDlss, application))
    {
    }

    UpscalerChoice Upscaler::getChoice() const
    {
        if (std::holds_alternative<DlssUpscaler>(mBackend))
            return UpscalerChoice::Dlss;
        return std::holds_alternative<FsrUpscaler>(mBackend) ? UpscalerChoice::Fsr : UpscalerChoice::Off;
    }

    UpscaleMasks Upscaler::getMasks() const
    {
        return getChoice() == UpscalerChoice::Fsr ? UpscaleMasks::Written : UpscaleMasks::Skipped;
    }

    UpscalerInput Upscaler::getInput() const
    {
        return getChoice() == UpscalerChoice::Dlss ? UpscalerInput::Noisy : UpscalerInput::Filtered;
    }

    std::optional<std::string_view> Upscaler::getRuntimeResources() const
    {
        if (getChoice() == UpscalerChoice::Dlss)
            return Dlss::Ngx::sOwnResourcePrefix;
        return std::nullopt;
    }

    FrameExtents Upscaler::extentsFor(const std::uint32_t width, const std::uint32_t height, const Upscale mode) const
    {
        return std::visit([&](const auto& backend) { return backend.extentsFor(width, height, mode); }, mBackend);
    }

    void Upscaler::prepare(const Upscale mode, const VkExtent2D render, const VkExtent2D output)
    {
        std::visit([&](auto& backend) { backend.prepare(mode, render, output); }, mBackend);
    }

    void Upscaler::measure(const Upscale mode, const VkExtent2D render, const VkExtent2D output)
    {
        std::visit([&](auto& backend) { backend.measure(mode, render, output); }, mBackend);
    }

    VkDeviceSize Upscaler::getHeldOutside() const
    {
        return std::visit([](const auto& backend) { return backend.getHeldOutside(); }, mBackend);
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
        return std::visit([&](const auto& backend) { return backend.bytesAt(render, output); }, mBackend);
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

    HandedImage Upscaler::record(const VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        return std::visit([&](auto& backend) { return backend.record(commands, inputs, timer); }, mBackend);
    }

    std::string_view Upscaler::describe() const
    {
        return std::visit([](const auto& backend) { return std::decay_t<decltype(backend)>::describe(); }, mBackend);
    }
}
