#include "frametracer.hpp"

#include <utility>

#include <components/rtxvulkan/trace/tracepast.hpp>

namespace Rtx
{
    FrameTracer::FrameTracer(const Device& device, const TracePasses& passes, const RadianceWidth radiance)
        : mDevice(device)
        , mChain(device, passes, sFrameSlots, radiance, TracePast::Kept)
        , mUpscaler(device)
    {
    }

    void FrameTracer::declare(const VkExtent2D render, const VkExtent2D output, const Upscale upscale,
        const RadianceWidth radiance, std::vector<ArenaImage>& into)
    {
        TraceChain::declareShared(render.width, render.height, radiance, into);
        if (upscales(upscale))
            Upscaler::declareShared(render, output, into);
    }

    VkDeviceSize FrameTracer::bytesAt(const Device& device, const VkExtent2D render, const VkExtent2D output,
        const Upscale upscale, const RadianceWidth radiance, const std::span<const ArenaImage> declared)
    {
        VkDeviceSize bytes = TraceChain::bytesAt(device, render.width, render.height, radiance, TracePast::Kept)
            + ImageArena::bytesFor(device, declared);
        if (upscales(upscale))
            bytes += Upscaler::bytesAt(device, render, output);
        return bytes;
    }

    FrameTracer::Moves FrameTracer::movesTo(const VkExtent2D render, const VkExtent2D output, const Upscale upscale,
        const std::vector<ArenaImage>& declared) const
    {
        const bool traceMoves = render.width != mChain.getWidth() || render.height != mChain.getHeight();
        return Moves{
            .mTrace = mChain.isBuilt() && traceMoves,
            .mUpscaler = mUpscaler.isBuilt() && !(upscales(upscale) && mUpscaler.isAt(render, output)),
            .mArena = declared != mDeclared,
        };
    }

    void FrameTracer::release(const Moves& moves)
    {
        if (moves.mTrace)
            mChain.release();
        if (moves.mUpscaler)
            mUpscaler.release();
        if (moves.mArena)
            mArena = ImageArena();
    }

    void FrameTracer::resize(
        const VkExtent2D render, const VkExtent2D output, const Upscale upscale, std::vector<ArenaImage>&& declared)
    {
        if (declared == mDeclared)
            return;

        mArena = ImageArena(mDevice, declared);
        mDeclared = std::move(declared);
        mChain.resize(render.width, render.height, ArenaRun(mArena, 0, TraceChain::getSharedCount()));

        // An upscaler a mode turned off keeps nothing but its pipelines: its images went with the
        // mode, as `release` let them go.
        if (upscales(upscale))
            mUpscaler.resize(
                render, output, ArenaRun(mArena, TraceChain::getSharedCount(), Upscaler::getSharedCount()));
    }
}
