#pragma once

#include <components/rtx/shaders/sky.h>

namespace Rtx
{
    /// Where Morrowind's atmosphere fades the fog colour to the sky colour, `Shaders::SkyRamp`, and
    /// what that fade is worth to a surface facing the sky. Read off the mesh the rasterizer draws
    /// it with, because the mesh is content: a sky mod's atmosphere is read the same way.
    ///
    /// **A fresh one is no atmosphere**: the fog colour everywhere, as the rasterizer draws with no
    /// mesh to draw.
    struct Atmosphere
    {
        Shaders::SkyRamp mRamp{ .mBottom = 2.0f, .mTop = 2.0f };

        /// The cosine-weighted mean of `Shaders::skyShare` over the hemisphere: how much of the sky
        /// colour a surface facing the sky receives, against the fog colour's `1 - mZenithShare`.
        /// 0.9076 on Morrowind's own mesh.
        float mZenithShare = 0.0f;
    };

    /// `Atmosphere::mZenithShare` of `ramp`, in closed form.
    float zenithShareOf(const Shaders::SkyRamp& ramp);
}
