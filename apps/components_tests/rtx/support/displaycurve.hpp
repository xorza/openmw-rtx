#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/look.h>

namespace Rtx::Testing
{
    /// A linear value as the display curve writes it, so a test can name the byte it expects.
    ///
    /// **The display curve and not the whole of what `tone.comp` does**: that pass grades and runs
    /// `toneMap` first, and a test measuring what the trace computed wants the radiance rather
    /// than the picture made of it. A visibility frame's `byte` encodes with this for the same reason.
    inline std::uint8_t encodeSrgb(float linear)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(Shaders::encodeSrgb(linear), 0.0f, 1.0f) * 255.0f));
    }

    /// The level `tone.comp` stores for a grey that reaches it at `linear`, exposure applied, under
    /// whatever the grade's dials in `look.h` say: in steps of the eight-bit store, before the store
    /// rounds it. The contrast grade, then the curve's own grey (`toneMapGrey`), then the display's
    /// encoding; the saturation grade leaves a grey where it is.
    inline float displayedLevel(float linear)
    {
        const float graded = linear * Shaders::contrastScale(linear, Shaders::TONE_CONTRAST);
        return std::clamp(Shaders::encodeSrgb(Shaders::toneMapGrey(graded)), 0.0f, 1.0f) * 255.0f;
    }

    /// The grey whose `displayedLevel` is `level`, by bisection over the straight stretch: what a
    /// test that wants a level between two bytes exposes a white surface at.
    inline float greyForLevel(float level)
    {
        float low = 0.1f;
        float high = 0.5f;
        for (int step = 0; step < 40; ++step)
        {
            const float middle = 0.5f * (low + high);
            (displayedLevel(middle) < level ? low : high) = middle;
        }
        return 0.5f * (low + high);
    }

    /// The byte `tone.comp` writes for that grey: `displayedLevel` rounded, as the store rounds it.
    inline std::uint8_t displayedGrey(float linear)
    {
        return static_cast<std::uint8_t>(std::lround(displayedLevel(linear)));
    }
}
