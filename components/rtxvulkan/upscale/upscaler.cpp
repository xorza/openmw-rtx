#include "upscaler.hpp"

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>

#include "ngx.hpp"

namespace Rtx
{
    std::string describeUpscalerChoice(const std::string_view running, const UpscalerChoice asked,
        const DlssAvailability dlss, const std::string_view refusal)
    {
        std::string said = std::format("{} (asked {}", running, sUpscalerNames.name(asked));
        if (asked == UpscalerChoice::Auto)
            switch (dlss)
            {
                case DlssAvailability::NotNvidia:
                    said += ", DLSS passed over: the device is not NVIDIA's";
                    break;
                case DlssAvailability::NotInstalled:
                    said += ", DLSS passed over: its library is not installed";
                    break;
                case DlssAvailability::Refused:
                    said += std::format(", DLSS passed over: it could not start, {}", refusal);
                    break;
                case DlssAvailability::Present:
                case DlssAvailability::Ready:
                    break;
            }
        return said + ')';
    }

    Upscaler::Backend Upscaler::makeBackend(const Device& device, const UpscalerChoice choice,
        DlssAvailability& availability, std::string& refusal, const Dlss::Application* application)
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
                refusal = refused.what();
                Log(Debug::Warning) << "NVIDIA DLSS could not start, so FSR is used: " << refused.what();
            }
        }

        return Backend(std::in_place_type<FsrUpscaler>, device);
    }

    Upscaler::Upscaler(const Device& device, const UpscalerChoice choice, const DlssAvailability availability,
        const Dlss::Application* application)
        : mAsked(choice)
        , mDlss(availability)
        , mBackend(makeBackend(device, choice, mDlss, mRefusal, application))
    {
    }

    UpscalerKind Upscaler::getKind() const
    {
        return std::holds_alternative<DlssUpscaler>(mBackend) ? UpscalerKind::Dlss : UpscalerKind::Fsr;
    }

    UpscalerChoice Upscaler::getChoice() const
    {
        return getKind() == UpscalerKind::Dlss ? UpscalerChoice::Dlss : UpscalerChoice::Fsr;
    }

    UpscaleMasks Upscaler::getMasks() const
    {
        return getKind() == UpscalerKind::Fsr ? UpscaleMasks::Written : UpscaleMasks::Skipped;
    }

    UpscalerInput Upscaler::getInput() const
    {
        return getKind() == UpscalerKind::Dlss ? UpscalerInput::Noisy : UpscalerInput::Filtered;
    }

    std::optional<std::string_view> Upscaler::getRuntimeResources() const
    {
        if (getKind() == UpscalerKind::Dlss)
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

    std::string Upscaler::describeChoice() const
    {
        return describeUpscalerChoice(describe(), mAsked, mDlss, mRefusal);
    }
}
