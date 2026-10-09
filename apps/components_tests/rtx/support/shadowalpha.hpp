#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include <components/rtx/shaders/gbuffer.h>

namespace Rtx::Testing
{
    /// Whether the ray whose shadow alpha this is got through: `Shaders::packShadowAlpha`'s sign, read
    /// as the device's `shadowOpen` reads it.
    inline bool shadowOpen(float alpha)
    {
        return !std::signbit(alpha);
    }

    /// The same as the one or nought the composite multiplies by where nothing filters.
    inline float shadowBit(float alpha)
    {
        return shadowOpen(alpha) ? 1.0f : 0.0f;
    }

    /// Each pixel's penumbra out of a shadow channel read back as four floats a pixel.
    inline void penumbraeOf(const std::vector<float>& channel, std::vector<float>& into)
    {
        into.resize(channel.size() / 4);
        for (std::size_t pixel = 0; pixel < into.size(); ++pixel)
            into[pixel] = Shaders::shadowPenumbra(channel[pixel * 4 + 3]);
    }
}
