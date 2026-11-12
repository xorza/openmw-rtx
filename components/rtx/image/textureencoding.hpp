#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// What a texture's texels are: a colour, which the file holds display-encoded and the hardware decodes inside the
    /// filter; a painted colour, which is a colour that is a lit picture; data, which is read as it is stored; or a
    /// normal map, whose channels are a direction that the sRGB curve would bend toward one corner. The file does not
    /// say which it is; what a surface binds it as does, so the texture table keys a slot on it as it keys one on the
    /// wrap.
    ///
    /// **A painted colour is one encoding with its own made chain**: Morrowind's textures were lit before they were
    /// saved, so the host divides the light painted into one out of each level (`ChainBuild::build`). One file bound
    /// both ways is two textures, as an engine imports a source once for each use (`docs/rtx/architecture.md`, "A
    /// painted colour is stored delit, keyed by use").
    ///
    /// **A normal map is an encoding of its own because of what is made beside it**, and not because of how it is
    /// stored: its companion is the roughness each of its levels loses to the normals it averages
    /// (`TextureCompanion::Spread`), where every other's is neutral.
    enum class TextureEncoding : std::uint8_t
    {
        Colour = 0,
        Data = 1,
        Normal = 2,
        Painted = 3,

        /// A classic pair's base colour and its metal-roughness map, each made on the host of the
        /// pair's diffuse and its classic `_spec` together (`ConversionPass`), so a slot of one is keyed
        /// by both files.
        ConvertedColour = 4,
        ConvertedSpecular = 5,
    };

    inline constexpr std::size_t sTextureEncodingCount
        = static_cast<std::size_t>(TextureEncoding::ConvertedSpecular) + 1;

    /// What a crash report names an encoding by (`ContentPreprocessor::chain`).
    inline constexpr NamedEnum sTextureEncodings{ std::array{
        std::pair{ TextureEncoding::Colour, std::string_view("colour") },
        std::pair{ TextureEncoding::Data, std::string_view("data") },
        std::pair{ TextureEncoding::Normal, std::string_view("normal") },
        std::pair{ TextureEncoding::Painted, std::string_view("painted") },
        std::pair{ TextureEncoding::ConvertedColour, std::string_view("converted colour") },
        std::pair{ TextureEncoding::ConvertedSpecular, std::string_view("converted specular") },
    } };
    static_assert(sTextureEncodings.mNames.size() == sTextureEncodingCount);

    /// Whether `encoding`'s texels are display-encoded: a colour, painted, converted or as it stands.
    inline constexpr bool isColour(const TextureEncoding encoding)
    {
        return encoding == TextureEncoding::Colour || encoding == TextureEncoding::Painted
            || encoding == TextureEncoding::ConvertedColour;
    }

    /// Whether a slot of `encoding` is made of two files: a classic pair's.
    inline constexpr bool isConverted(const TextureEncoding encoding)
    {
        return encoding == TextureEncoding::ConvertedColour || encoding == TextureEncoding::ConvertedSpecular;
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
