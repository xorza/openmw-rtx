#pragma once

#include <components/rtx/shaders/visibility.h>

namespace Rtx
{
    /// What one frame hands every denoising pass beside the images.
    struct DenoiseFrame
    {
        /// What the trace sampled: both eyes, the previous frame's basis, and the far plane.
        const Shaders::VisibilityConstants& mSampled;

        /// What a world distance is multiplied by before a surface history holds it —
        /// `HistoryConstants::mDistanceScale` says why — worked out once for the frame.
        float mDistanceScale;
    };
}
