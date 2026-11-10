#include "specularalbedo.hpp"

#include <cstddef>
#include <cstdint>

#include <components/rtx/shaders/brdf.h>

namespace Rtx
{
    osg::Vec2f specularAlbedoAt(float cosine, float roughness)
    {
        const Shaders::SpecularTableTaps taps = Shaders::specularTableTaps(cosine, roughness);

        const auto node = [&](std::uint32_t column, std::uint32_t row) {
            const std::size_t at = (std::size_t{ row } * Shaders::SPECULAR_TABLE_SIZE + column) * 2;
            return osg::Vec2f(sSpecularAlbedo[at], sSpecularAlbedo[at + 1]);
        };

        return Shaders::specularTableBlend(taps, node(taps.mLeft, taps.mTop), node(taps.mRight, taps.mTop),
            node(taps.mLeft, taps.mBottom), node(taps.mRight, taps.mBottom));
    }
}
