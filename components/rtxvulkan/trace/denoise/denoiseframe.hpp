#pragma once

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/shaders/shared/accumulate.h>

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

        /// `Reconstruction::mFilters`: how the bounce is filtered past the wavelet.
        FilterSwitches mFilters;

        /// What every temporal filter's history is handed this frame, from the one place each
        /// field has: `fresh` where the history holds nothing to reuse.
        Shaders::HistoryConstants history(bool fresh) const
        {
            return Shaders::HistoryConstants{
                .mEyes = mSampled.mEyes,
                .mReset = fresh ? 1u : 0u,
                .mDistanceScale = mDistanceScale,
                .mPreviousJitter = mSampled.mPreviousJitter,
                .mPrevious = mSampled.mPrevious,
                .mArmsSpread = mSampled.mArmsSpread,
            };
        }
    };
}
