#pragma once

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

        /// `Reconstruction::mAntilag`: whether the accumulator holds its slow mean to its fast one.
        bool mAntilag;

        /// `Reconstruction::mHistoryFix`: whether the wavelet's first level rebuilds a short history
        /// from the surface around it.
        bool mHistoryFix;

        /// `Reconstruction::mDualMotion`: whether a surface the previous frame did not see takes the
        /// accumulator's history along its occluder's motion.
        bool mDualMotion;

        /// `Reconstruction::mAntiFirefly`: whether the accumulator holds a short history of the bounce
        /// under the light around it.
        bool mAntiFirefly;

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
