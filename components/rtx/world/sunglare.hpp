#pragma once

#include <osg/Vec3f>

#include <components/rtx/shaders/visibility.h>
#include <components/sky/sunglarefader.hpp>

namespace Rtx
{
    /// The sun glare fader, as the game states it — `glare.h`. A wash over the picture after the
    /// curve, so the display chain's and no trace's: it rides beside the frame block, not in it.
    struct SunGlare
    {
        /// The wash as the content sets it, which the rasterizer's `SunGlareCallback` reads too.
        Sky::SunGlareFader mFader{};

        /// The time-of-day fade times the weather's `Glare_View` times the disc's own alpha, and
        /// nought where no sun is drawn: what `SunGlareCallback` multiplies the fader's most by.
        float mFade = 0.0f;

        /// How much of the colour `frame` lays over the picture before the share of the sun the
        /// eye could see is multiplied in: the fader's most, by the fade, by how far the eye's axis
        /// stands from the sun (`Sky::SunGlareFader::atAngle`). Nought for no fader, and nought
        /// past the angle.
        float amountFor(const Shaders::VisibilityConstants& frame) const;
    };
}
