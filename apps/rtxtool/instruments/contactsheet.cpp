#include "contactsheet.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/image/colour.hpp>
#include <components/rtx/image/shadingmap.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/scene/texturetable.hpp>

namespace RtxTool
{
    namespace
    {
        /// How large one thumbnail is drawn, and how far apart the pairs stand.
        constexpr std::uint32_t sThumbnail = 128;
        constexpr std::uint32_t sGap = 6;

        /// How many pairs stand across the sheet.
        constexpr std::uint32_t sColumns = 6;

        /// The side of a square of the checker a texture with no colour to read is drawn as.
        constexpr std::uint32_t sCheckerSide = 8;
    }

    std::uint32_t ContactSheet::getStride()
    {
        return sThumbnail + sGap;
    }

    std::uint32_t ContactSheet::getThumbnail()
    {
        return sThumbnail;
    }

    std::uint32_t ContactSheet::getLeftOf(std::uint32_t index) const
    {
        return sGap + index % sColumns * (sThumbnail * 2 + sGap + sGap);
    }

    std::uint32_t ContactSheet::getTopOf(std::uint32_t index) const
    {
        return sGap + index / sColumns * (sThumbnail + sGap);
    }

    ContactSheet drawContactSheet(std::span<const Rtx::TextureData> textures)
    {
        if (textures.empty())
            return ContactSheet{};

        const auto count = static_cast<std::uint32_t>(textures.size());
        const std::uint32_t rows = (count + sColumns - 1) / sColumns;
        const std::uint32_t pairWidth = sThumbnail * 2 + sGap;
        const std::uint32_t width = sColumns * pairWidth + (sColumns + 1) * sGap;
        const std::uint32_t height = rows * (sThumbnail + sGap) + sGap;

        // Mid grey behind them, so a texture that is black and one that is missing do not look the
        // same as the paper they are printed on.
        std::vector<std::uint8_t> sheet(std::size_t{ width } * height * 4, std::uint8_t{ 64 });
        for (std::size_t at = 3; at < sheet.size(); at += 4)
            sheet[at] = 255;

        ContactSheet drawn{ std::move(sheet), width, height, count };
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const Rtx::TextureData& texture = textures[index];
            const std::uint32_t left = drawn.getLeftOf(index);
            const std::uint32_t top = drawn.getTopOf(index);

            // **What has no colour to read is drawn as a checker**, both halves of its pair: a bake
            // and a composite carry no bytes, since the device makes them, and BC5 is two data
            // channels. Read, a bake would index a level it does not have and BC5's second block
            // would show as colour. The legend still names the pair.
            if (!Rtx::readsColour(texture))
            {
                for (const std::uint32_t offset : { 0u, ContactSheet::getStride() })
                    for (std::uint32_t y = 0; y < sThumbnail; ++y)
                        for (std::uint32_t x = 0; x < sThumbnail; ++x)
                        {
                            const std::uint8_t shade = (x / sCheckerSide + y / sCheckerSide) % 2 == 0 ? 32 : 96;
                            const std::size_t at = (std::size_t{ top + y } * width + left + offset + x) * 4;
                            for (int channel = 0; channel < 3; ++channel)
                                drawn.mPixels[at + channel] = shade;
                        }
                continue;
            }

            // The estimate a painted colour's chain is delit by (`ChainBuild`), made here for the
            // sheet off the file, and none for a texture nothing delights.
            const std::optional<Rtx::ShadingMap> painted = texture.mEncoding != Rtx::TextureEncoding::Painted
                ? std::nullopt
                : std::optional<Rtx::ShadingMap>(std::in_place, texture);

            for (std::uint32_t y = 0; y < sThumbnail; ++y)
                for (std::uint32_t x = 0; x < sThumbnail; ++x)
                {
                    // Nearest neighbour, and the centre of the texel a thumbnail pixel covers.
                    const float u = (x + 0.5f) / sThumbnail;
                    const float v = (y + 0.5f) / sThumbnail;
                    const auto texelX = std::min(
                        static_cast<std::uint32_t>(u * static_cast<float>(texture.mWidth)), texture.mWidth - 1);
                    const auto texelY = std::min(
                        static_cast<std::uint32_t>(v * static_cast<float>(texture.mHeight)), texture.mHeight - 1);

                    const osg::Vec3f stored = Rtx::texelAt(texture, texture.mLevels.front(), texelX, texelY);
                    osg::Vec3f corrected = stored;
                    if (painted.has_value())
                    {
                        // **In linear, because that is where the chain divides.** A painted colour's
                        // bytes are display-encoded, and a sheet that divided the bytes would show a
                        // correction the renderer never applies, half again too strong in the darks.
                        const osg::Vec3f divided = Rtx::delit(
                            Rtx::toLinear(stored), Rtx::paintedLight(painted->getValues(), u, v, texture.mWrap));
                        for (int channel = 0; channel < 3; ++channel)
                            corrected[channel] = Rtx::toEncoded(divided[channel]);
                    }

                    const auto place = [&](std::uint32_t offset, const osg::Vec3f& colour) {
                        const std::size_t at = (std::size_t{ top + y } * width + left + offset + x) * 4;
                        for (int channel = 0; channel < 3; ++channel)
                            drawn.mPixels[at + channel] = static_cast<std::uint8_t>(
                                std::lround(std::clamp(colour[channel], 0.0f, 1.0f) * 255.0f));
                    };

                    place(0, stored);
                    place(ContactSheet::getStride(), corrected);
                }
        }

        return drawn;
    }

    void listSheetNames(
        std::span<const Rtx::TextureData> drawn, std::span<const Rtx::TextureRow> rows, std::vector<std::string>& into)
    {
        for (const Rtx::TextureData& texture : drawn)
            into.push_back(rows[texture.mSlot].getName());
    }

}
