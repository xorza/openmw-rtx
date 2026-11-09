#include "upscaler.hpp"

#include <cassert>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <components/debug/debuglog.hpp>
#include <components/platform/dlss.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/dlss.hpp>

namespace Rtx
{
    Upscaler::Upscaler(const Device& device, const UpscalerKind requested)
    {
        // **The library's presence is part of what `auto` means**, and the query NGX answers
        // before a device exists can be inconclusive without it. Asking the same discovery the
        // launcher and the settings window ask keeps the menu, the picture and the log agreeing;
        // an explicit `dlss` still tries and fails by name.
        if (resolveUpscaler(requested, Dlss::sCompiled, device.isDlssCandidate(), Platform::Dlss::installed())
            == UpscalerKind::Dlss)
        {
            try
            {
                mBackend.emplace(std::in_place_type<DlssUpscaler>, device);
            }
            catch (const Unsupported& refused)
            {
                if (requested == UpscalerKind::Dlss)
                    throw;
                Log(Debug::Warning) << "NVIDIA DLSS could not start, so FSR is used: " << refused.what();
            }
        }

        if (!mBackend.has_value())
            mBackend.emplace(std::in_place_type<FsrUpscaler>, device);
    }

    Upscaler::~Upscaler() = default;

    UpscalerKind Upscaler::getKind() const
    {
        assert(mBackend.has_value() && "an upscaler asked for its kind before it was made");
        return std::holds_alternative<DlssUpscaler>(*mBackend) ? UpscalerKind::Dlss : UpscalerKind::Fsr;
    }

    void Upscaler::resize(const Upscale mode, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        assert(mBackend.has_value() && "an upscaler resized before it was made");
        std::visit(
            [&](auto& backend) {
                using Backend = std::decay_t<decltype(backend)>;
                if constexpr (std::is_same_v<Backend, DlssUpscaler>)
                    backend.resize(mode, render, output, shared);
                else
                    backend.resize(render, output, shared);
            },
            *mBackend);
    }

    void Upscaler::declareShared(
        const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output, std::vector<ArenaImage>& into)
    {
        assert(kind != UpscalerKind::Auto && "an upscaler kind that was never resolved");
        if (kind == UpscalerKind::Dlss)
            DlssUpscaler::declareShared(render, output, into);
        else
            FsrUpscaler::declareShared(render, output, into);
    }

    std::size_t Upscaler::getSharedCount(const UpscalerKind kind)
    {
        return kind == UpscalerKind::Dlss ? DlssUpscaler::getSharedCount() : FsrUpscaler::getSharedCount();
    }

    VkDeviceSize Upscaler::bytesAt(
        const Device& device, const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output)
    {
        return kind == UpscalerKind::Dlss ? DlssUpscaler::bytesAt(device, render, output)
                                          : FsrUpscaler::bytesAt(device, render, output);
    }

    void Upscaler::release()
    {
        std::visit([](auto& backend) { backend.release(); }, *mBackend);
    }

    bool Upscaler::isBuilt() const
    {
        return std::visit([](const auto& backend) { return backend.isBuilt(); }, *mBackend);
    }

    bool Upscaler::isAt(const VkExtent2D render, const VkExtent2D output) const
    {
        return std::visit([&](const auto& backend) { return backend.isAt(render, output); }, *mBackend);
    }

    void Upscaler::reset()
    {
        std::visit([](auto& backend) { backend.reset(); }, *mBackend);
    }

    bool Upscaler::isFresh() const
    {
        return std::visit([](const auto& backend) { return backend.isFresh(); }, *mBackend);
    }

    HandedImage Upscaler::record(const VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        assert(mBackend.has_value() && "an upscaler recorded before it was made");
        return std::visit([&](auto& backend) { return backend.record(commands, inputs, timer); }, *mBackend);
    }

    std::string_view Upscaler::describe() const
    {
        return std::visit(
            [](const auto& backend) {
                using Backend = std::decay_t<decltype(backend)>;
                return Backend::describe();
            },
            *mBackend);
    }
}
