#include "upscale.hpp"

#include <cassert>

namespace Rtx
{
    FrameExtents extentsFor(const std::uint32_t outputWidth, const std::uint32_t outputHeight, const Upscale mode)
    {
        const float ratio = upscaleRatio(mode);
        return FrameExtents{
            .mRenderWidth = static_cast<std::uint32_t>(static_cast<float>(outputWidth) / ratio),
            .mRenderHeight = static_cast<std::uint32_t>(static_cast<float>(outputHeight) / ratio),
            .mOutputWidth = outputWidth,
            .mOutputHeight = outputHeight,
        };
    }

    std::uint32_t jitterPhasesFor(const std::uint32_t renderWidth, const std::uint32_t outputWidth)
    {
        assert(renderWidth > 0 && renderWidth <= outputWidth && "a render extent that is not the output's or less");

        // The square as a product and not `pow`, which the SDK calls: the two agree to the last place
        // or one off it, so a truncation can tell them apart only where the count lands within that
        // of a whole number — and at 1920 wide the fixed ratios land on 72, 32, 23.14, 18 and 8,
        // where the whole ones are exact in both.
        const float ratio = static_cast<float>(outputWidth) / static_cast<float>(renderWidth);
        return static_cast<std::uint32_t>(8.0f * ratio * ratio);
    }
}
