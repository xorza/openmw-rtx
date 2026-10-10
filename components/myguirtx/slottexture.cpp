#include "slottexture.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <osg/GL>
#include <osg/Image>
#include <osg/Vec4f>

#include <components/debug/debuglog.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace MyGUIRtx
{
    namespace
    {
        /// Where each of a pixel's four channels comes from in a format's own bytes, or none for an
        /// alpha the format has not got, which is opaque.
        struct ByteLayout
        {
            std::size_t mBytes = 0;
            std::array<int, 4> mFrom{};
        };

        /// The byte formats the game hands a picture in, each read as GL samples it: the colour as
        /// stored, a luminance three times, and an alpha of one where the format holds none — an X8
        /// file's spare byte among them. Nothing for any other format.
        std::optional<ByteLayout> byteLayoutOf(const Rtx::TextureFormat format)
        {
            switch (format)
            {
                case Rtx::TextureFormat::Rgba8Srgb:
                    return ByteLayout{ 4, { 0, 1, 2, 3 } };
                case Rtx::TextureFormat::Bgra8Srgb:
                    return ByteLayout{ 4, { 2, 1, 0, 3 } };
                case Rtx::TextureFormat::Xbgr8:
                    return ByteLayout{ 4, { 0, 1, 2, -1 } };
                case Rtx::TextureFormat::Xrgb8:
                    return ByteLayout{ 4, { 2, 1, 0, -1 } };
                case Rtx::TextureFormat::Rgb8:
                    return ByteLayout{ 3, { 0, 1, 2, -1 } };
                case Rtx::TextureFormat::Bgr8:
                    return ByteLayout{ 3, { 2, 1, 0, -1 } };
                case Rtx::TextureFormat::Luminance:
                    return ByteLayout{ 1, { 0, 0, 0, -1 } };
                case Rtx::TextureFormat::LuminanceAlpha:
                    return ByteLayout{ 2, { 0, 0, 0, 1 } };
                default:
                    return std::nullopt;
            }
        }
    }

    SlotTexture::SlotTexture(std::string name, Rtx::GuiRenderer& renderer)
        : mRenderer(renderer)
        , mName(std::move(name))
    {
    }

    SlotTexture::~SlotTexture()
    {
        drop();
    }

    void SlotTexture::take(const int width, const int height)
    {
        drop();

        mWidth = width;
        mHeight = height;
        mSlot = mRenderer.addGuiTexture(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    }

    void SlotTexture::drop()
    {
        if (!mSlot.isNone())
            mRenderer.dropGuiTexture(mSlot);

        mSlot = Rtx::GuiSlot::none();
        mWidth = 0;
        mHeight = 0;
    }

    Rtx::GuiRegion SlotTexture::whole() const
    {
        return Rtx::GuiRegion{ 0, 0, static_cast<std::uint32_t>(mWidth), static_cast<std::uint32_t>(mHeight) };
    }

    void SlotTexture::sendImage(const osg::Image& image)
    {
        assert(image.s() == mWidth && image.t() == mHeight && "an image sent into a slot of another size");

        // A size the renderer refused, which it said in the log: the texture draws as nothing.
        if (mSlot.isNone())
            return;

        std::uint8_t* into = mRenderer.lendGuiTexture(mSlot, whole()).data();
        const std::size_t bytes = static_cast<std::size_t>(mWidth) * mHeight * 4;
        const Rtx::TextureFormat format = Rtx::readFormat(image, Rtx::TextureEncoding::Colour);
        if (format == Rtx::TextureFormat::Rgba8Srgb && image.isDataContiguous() && image.getTotalSizeInBytes() == bytes)
            std::memcpy(into, image.data(), bytes);
        else if (const std::optional<ByteLayout> layout = byteLayoutOf(format))
        {
            // Byte by byte, a row at a time for a row the image pads: `getColor` is a call and a
            // conversion through floats a pixel, half a second for the global map's 33 million.
            for (int y = 0; y < mHeight; ++y)
            {
                const std::uint8_t* from = image.data(0, y);
                for (int x = 0; x < mWidth; ++x, into += 4, from += layout->mBytes)
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        into[channel] = layout->mFrom[channel] < 0 ? 0xFF : from[layout->mFrom[channel]];
            }
        }
        else if (Rtx::isWidened(format))
        {
            // **What the core widens is read as the core widens it**: an old mod's sixteen-bit
            // packed words and the wider loose channels, of which `getColor` reads none of the
            // packed ones and draws them white. RGBA8, rows tight, as the slot holds them.
            std::vector<Rtx::MipLevel> levels;
            std::vector<std::byte> texels;
            const Misc::Result<Rtx::TextureData, std::string> described
                = Rtx::describeFinestLevel(image, levels, texels);
            if (described.isOk() && described.value().mBytes.size() >= bytes)
                std::memcpy(into, described.value().mBytes.data(), bytes);
            else
            {
                // Written either way, because what a lend hands over holds whatever it last held.
                std::memset(into, 0, bytes);
                Log(Debug::Warning) << "The interface texture \"" << mName << "\" is drawn blank: "
                                    << (described.isOk() ? "its finest level is short" : described.error());
            }
        }
        else
        {
            for (int y = 0; y < mHeight; ++y)
                for (int x = 0; x < mWidth; ++x, into += 4)
                {
                    const osg::Vec4f colour = image.getColor(x, y);
                    into[0] = static_cast<std::uint8_t>(std::clamp(colour.r(), 0.f, 1.f) * 255.f + 0.5f);
                    into[1] = static_cast<std::uint8_t>(std::clamp(colour.g(), 0.f, 1.f) * 255.f + 0.5f);
                    into[2] = static_cast<std::uint8_t>(std::clamp(colour.b(), 0.f, 1.f) * 255.f + 0.5f);
                    into[3] = static_cast<std::uint8_t>(std::clamp(colour.a(), 0.f, 1.f) * 255.f + 0.5f);
                }
        }

        mRenderer.sendGuiTexture(mSlot);
    }

    void SlotTexture::saveToFile(const std::string& fname)
    {
        Log(Debug::Warning) << "Would save image to file " << fname;
    }

    void SlotTexture::setShader(const std::string& /*shaderName*/)
    {
        Log(Debug::Warning) << "Texture::setShader is not implemented";
    }
}
