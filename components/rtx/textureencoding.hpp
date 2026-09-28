#pragma once

#include <cstddef>
#include <cstdint>

namespace Rtx
{
    /// What a texture's texels are: a colour, which the file holds display-encoded and the hardware
    /// decodes inside the filter; data, which is read as it is stored; or a normal map, which is data
    /// that something is measured on as it arrives. A normal map's channels are a direction, and the
    /// sRGB curve under it would bend every normal toward one corner. The file does not say which it
    /// is; what a surface binds it as does, so the texture table keys a slot on it as it keys one on
    /// the wrap.
    ///
    /// **A normal map is an encoding of its own because of what is made beside it**, and not
    /// because of how it is stored: its companion is the roughness each of its levels loses to the
    /// normals it averages (`TextureCompanion::Spread`), where a colour's is the light painted into
    /// it and other data's is neutral.
    enum class TextureEncoding : std::uint8_t
    {
        Colour = 0,
        Data = 1,
        Normal = 2,
    };

    inline constexpr std::size_t sTextureEncodingCount = static_cast<std::size_t>(TextureEncoding::Normal) + 1;

    /// What stands beside a texture at its slot of the companion array.
    enum class TextureCompanion : std::uint8_t
    {
        /// The neutral shading map: nothing to divide out.
        Neutral,

        /// The light painted into a colour file, estimated off its texels (`ShadingPass`).
        Shading,

        /// The roughness a normal map's levels lose to the normals they average (`NormalSpreadPass`).
        Spread,
    };
}
