#include "blockdecode.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <rgbcx/rgbcx.h>

namespace Rtx
{
    namespace
    {
        using Block = std::array<std::uint8_t, 16 * 4>;

        /// BC2's colour, which is BC1's in four colours whatever its endpoints' order: the
        /// three-colour mode is BC1's alone.
        void decodeFourColours(const std::uint8_t* bits, Block& into)
        {
            const auto endpoint = [](const std::uint16_t packed) {
                const std::uint32_t r = (packed >> 11) & 31;
                const std::uint32_t g = (packed >> 5) & 63;
                const std::uint32_t b = packed & 31;
                return std::array<std::uint32_t, 3>{ (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2) };
            };
            const auto c0 = endpoint(static_cast<std::uint16_t>(bits[0] | (bits[1] << 8)));
            const auto c1 = endpoint(static_cast<std::uint16_t>(bits[2] | (bits[3] << 8)));
            std::array<std::array<std::uint32_t, 3>, 4> palette{ c0, c1, {}, {} };
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                palette[2][channel] = (2 * c0[channel] + c1[channel]) / 3;
                palette[3][channel] = (c0[channel] + 2 * c1[channel]) / 3;
            }
            const std::uint32_t indices
                = bits[4] | (bits[5] << 8) | (bits[6] << 16) | (static_cast<std::uint32_t>(bits[7]) << 24);
            for (std::size_t texel = 0; texel < 16; ++texel)
                for (std::size_t channel = 0; channel < 3; ++channel)
                    into[texel * 4 + channel]
                        = static_cast<std::uint8_t>(palette[(indices >> (2 * texel)) & 3][channel]);
        }

        /// One block of `format` into sixteen texels; false for a format this does not decode.
        bool decodeBlock(const TextureFormat format, const std::uint8_t* bits, Block& into)
        {
            switch (format)
            {
                case TextureFormat::Bc1RgbaSrgb:
                case TextureFormat::Bc1RgbaUnorm:
                    rgbcx::unpack_bc1(bits, into.data(), true);
                    return true;
                case TextureFormat::Bc2Srgb:
                case TextureFormat::Bc2Unorm:
                    // The alpha first, four bits a texel from the low nibble, then BC1's colour.
                    decodeFourColours(bits + 8, into);
                    for (std::size_t texel = 0; texel < 16; ++texel)
                        into[texel * 4 + 3]
                            = static_cast<std::uint8_t>(((bits[texel / 2] >> (4 * (texel % 2))) & 15) * 17);
                    return true;
                case TextureFormat::Bc3Srgb:
                case TextureFormat::Bc3Unorm:
                    rgbcx::unpack_bc3(bits, into.data());
                    return true;
                case TextureFormat::Bc4Unorm:
                    rgbcx::unpack_bc4(bits, into.data(), 4);
                    for (std::size_t texel = 0; texel < 16; ++texel)
                    {
                        into[texel * 4 + 1] = into[texel * 4];
                        into[texel * 4 + 2] = into[texel * 4];
                        into[texel * 4 + 3] = 255;
                    }
                    return true;
                case TextureFormat::Bc5Unorm:
                    rgbcx::unpack_bc5(bits, into.data(), 0, 1, 4);
                    for (std::size_t texel = 0; texel < 16; ++texel)
                    {
                        into[texel * 4 + 2] = 0;
                        into[texel * 4 + 3] = 255;
                    }
                    return true;
                default:
                    return false;
            }
        }
    }

    bool decodeBlocks(const TextureFormat format, const std::span<const std::byte> blocks, const std::uint32_t width,
        const std::uint32_t height, const std::span<std::uint8_t> into)
    {
        if (format == TextureFormat::Unnamed)
            return false;
        const std::uint32_t across = (width + 3) / 4;
        const std::uint32_t down = (height + 3) / 4;
        const TexelLayout layout = layoutOf(format);
        const std::size_t blockBytes = layout.mBytes;
        Block texels{};
        if (layout.mSide != 4 || blocks.size() < layout.levelBytes(width, height)
            || into.size() < std::size_t{ width } * height * 4
            || !decodeBlock(format, reinterpret_cast<const std::uint8_t*>(blocks.data()), texels))
            return false;

        for (std::uint32_t by = 0; by < down; ++by)
            for (std::uint32_t bx = 0; bx < across; ++bx)
            {
                decodeBlock(format,
                    reinterpret_cast<const std::uint8_t*>(blocks.data())
                        + (std::size_t{ by } * across + bx) * blockBytes,
                    texels);
                const std::uint32_t columns = std::min(4u, width - bx * 4);
                for (std::uint32_t row = 0; row < 4 && by * 4 + row < height; ++row)
                    std::memcpy(into.data() + ((std::size_t{ by } * 4 + row) * width + bx * 4) * 4,
                        texels.data() + std::size_t{ row } * 16, std::size_t{ columns } * 4);
            }
        return true;
    }
}
