#include "upscaler.hpp"

#include <cassert>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>

namespace Rtx
{
    Upscaler::Backend Upscaler::makeBackend(const Device& device, const UpscalerKind requested,
        const DlssReadiness& readiness, const Dlss::Application* application)
    {
        // Each backend is made in place and returned as it is, which the backends, pinned to their
        // device, need: a prvalue initialises the member with no move.
        if (resolveUpscaler(requested, readiness) == UpscalerKind::Dlss)
        {
            try
            {
                return Backend(std::in_place_type<DlssUpscaler>, device, application);
            }
            catch (const Unsupported& refused)
            {
                if (requested == UpscalerKind::Dlss)
                    throw;
                Log(Debug::Warning) << "NVIDIA DLSS could not start, so FSR is used: " << refused.what();
            }
        }

        return Backend(std::in_place_type<FsrUpscaler>, device);
    }

    Upscaler::Upscaler(const Device& device, const UpscalerKind requested, const DlssReadiness& readiness,
        const Dlss::Application* application)
        : mBackend(makeBackend(device, requested, readiness, application))
    {
    }

    UpscalerKind Upscaler::getKind() const
    {
        return std::holds_alternative<DlssUpscaler>(mBackend) ? UpscalerKind::Dlss : UpscalerKind::Fsr;
    }

    void Upscaler::resize(const Upscale mode, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        std::visit([&](auto& backend) { backend.resize(mode, render, output, shared); }, mBackend);
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
        assert(kind != UpscalerKind::Auto && "an upscaler kind that was never resolved");
        return kind == UpscalerKind::Dlss ? DlssUpscaler::getSharedCount() : FsrUpscaler::getSharedCount();
    }

    VkDeviceSize Upscaler::bytesAt(
        const Device& device, const UpscalerKind kind, const VkExtent2D render, const VkExtent2D output)
    {
        assert(kind != UpscalerKind::Auto && "an upscaler kind that was never resolved");
        return kind == UpscalerKind::Dlss ? DlssUpscaler::bytesAt(device, render, output)
                                          : FsrUpscaler::bytesAt(device, render, output);
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
