#include "frametracer.hpp"

#include <cstddef>
#include <utility>

#include <components/rtxvulkan/frame/tracepast.hpp>

namespace Rtx
{
    FrameTracer::FrameTracer(const Device& device, const TracePasses& passes, const RadianceWidth radiance)
        : mDevice(device)
        , mChain(device, passes, sFrameSlots, radiance, TracePast::Kept)
    {
    }

    void FrameTracer::declare(const VkExtent2D render, const VkExtent2D output, const RenderProfile& profile,
        const Upscaler& upscaler, std::vector<ArenaImage>& into)
    {
        TraceChain::declareShared(render.width, render.height, profile.mRadianceWidth,
            profile.mReconstruction.mSampling.mBounceRays, rendererFilterUnder(profile.mUpscale, upscaler.getInput()),
            into);
        if (upscales(profile.mUpscale))
            upscaler.declareShared(render, output, into);
    }

    VkDeviceSize FrameTracer::bytesAt(const Device& device, const VkExtent2D render, const VkExtent2D output,
        const Upscale upscale, const Upscaler& upscaler, const RadianceWidth radiance,
        const std::span<const ArenaImage> declared)
    {
        VkDeviceSize bytes = TraceChain::bytesAt(device, render.width, render.height, radiance, TracePast::Kept,
                                 rendererFilterUnder(upscale, upscaler.getInput()), upscaler.getMasks())
            + ImageArena::bytesFor(device, declared);
        if (upscales(upscale))
            bytes += upscaler.bytesAt(render, output);
        return bytes;
    }

    FrameTracer::Moves FrameTracer::movesTo(const VkExtent2D render, const VkExtent2D output, const Upscale upscale,
        const std::vector<ArenaImage>& declared, const Upscaler& upscaler) const
    {
        const bool traceMoves = render.width != mChain.getWidth() || render.height != mChain.getHeight()
            || rendererFilterUnder(upscale, upscaler.getInput()) != mChain.getFilter();
        return Moves{
            .mTrace = mChain.isBuilt() && traceMoves,
            .mUpscaler = upscaler.isBuilt() && !(upscales(upscale) && upscaler.isAt(render, output)),
            .mArena = declared != mDeclared,
        };
    }

    void FrameTracer::release(const Moves& moves, Upscaler& upscaler)
    {
        if (moves.mTrace)
            mChain.release();
        if (moves.mUpscaler)
            upscaler.release();
        if (moves.mArena)
            mArena = ImageArena();
    }

    void FrameTracer::resize(const VkExtent2D render, const VkExtent2D output, const Upscale upscale,
        std::vector<ArenaImage>&& declared, Upscaler& upscaler)
    {
        if (declared == mDeclared)
            return;

        mArena = ImageArena(mDevice, declared);
        mDeclared = std::move(declared);
        const RendererFilter filter = rendererFilterUnder(upscale, upscaler.getInput());
        const std::size_t chained = TraceChain::getSharedCount(filter);
        mChain.resize(render.width, render.height, filter, ArenaRun(mArena, 0, chained));

        // An upscaler a mode turned off keeps nothing but its pipelines: its images went with the
        // mode, as `release` let them go.
        if (upscales(upscale))
            upscaler.resize(upscale, render, output, ArenaRun(mArena, chained, upscaler.getSharedCount()));
    }
}
