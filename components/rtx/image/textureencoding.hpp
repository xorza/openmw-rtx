#pragma once

#include <cstddef>
#include <cstdint>

namespace Rtx
{
    /// What a texture's texels are: a colour, which the file holds display-encoded and the hardware
    /// decodes inside the filter; a painted colour, which is a colour that is a lit picture; data, which
    /// is read as it is stored; or a normal map. A normal map's channels are a direction, and the sRGB
    /// curve under it would bend every normal toward one corner. The file does not say which it is;
    /// what a surface binds it as does, so the texture table keys a slot on it as it keys one on the
    /// wrap.
    ///
    /// **A painted colour is one encoding with its own made chain**: Morrowind's textures were lit
    /// before they were saved, so the host divides the light painted into one out of each level
    /// (`ChainBuild::buildPainted`). A metal-roughness base colour, an emissive map and a sheet are
    /// colours, read as they stand. One file bound both ways is two textures, as an engine imports a
    /// source once for each use (`.notes/content-cache.md`, colour files stored delit).
    ///
    /// **A normal map is an encoding of its own because of what is made beside it**, and not because
    /// of how it is stored: its companion is the roughness each of its levels loses to the normals it
    /// averages (`TextureCompanion::Spread`), where every other's is neutral.
    enum class TextureEncoding : std::uint8_t
    {
        Colour = 0,
        Data = 1,
        Normal = 2,
        Painted = 3,
    };

    inline constexpr std::size_t sTextureEncodingCount = static_cast<std::size_t>(TextureEncoding::Painted) + 1;

    /// Whether `encoding`'s texels are display-encoded: a colour, painted or not.
    inline constexpr bool isColour(const TextureEncoding encoding)
    {
        return encoding == TextureEncoding::Colour || encoding == TextureEncoding::Painted;
    }

    /// What stands beside a texture at its slot of the companion array.
    enum class TextureCompanion : std::uint8_t
    {
        /// Nothing measured.
        Neutral,

        /// The roughness a normal map's levels lose to the normals they average, measured on the
        /// device.
        Spread,
    };
}
