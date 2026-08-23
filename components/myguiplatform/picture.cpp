#include "picture.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>

#include <osg/Image>

namespace
{

    /// What MyGUI can be handed a row at a time, or nothing where the image has to be read pixel by
    /// pixel first. The common cases — a decoded frame, a screenshot, a map — are all in here.
    std::optional<MyGUI::PixelFormat> directFormat(const osg::Image& image)
    {
        if (image.getDataType() != GL_UNSIGNED_BYTE || !image.isDataContiguous())
            return {};

        switch (image.getPixelFormat())
        {
            case GL_LUMINANCE:
                return MyGUI::PixelFormat::L8;
            case GL_LUMINANCE_ALPHA:
                return MyGUI::PixelFormat::L8A8;
            case GL_RGB:
                return MyGUI::PixelFormat::R8G8B8;
            case GL_RGBA:
                return MyGUI::PixelFormat::R8G8B8A8;
            default:
                return {};
        }
    }

    std::size_t bytesPerPixel(MyGUI::PixelFormat format)
    {
        switch (format.getValue())
        {
            case MyGUI::PixelFormat::L8:
                return 1;
            case MyGUI::PixelFormat::L8A8:
                return 2;
            case MyGUI::PixelFormat::R8G8B8:
                return 3;
            default:
                return 4;
        }
    }

    std::uint8_t channel(float value)
    {
        return static_cast<std::uint8_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
    }

}

namespace MyGUIPlatform
{

    Picture::Picture(std::string_view label)
    {
        static unsigned int next = 0;
        mName = std::string(label) + " " + std::to_string(next++);
    }

    Picture::~Picture()
    {
        if (mTexture != nullptr)
            MyGUI::RenderManager::getInstance().destroyTexture(mTexture);
    }

    Picture::Picture(Picture&& other) noexcept
        : mName(std::move(other.mName))
        , mTexture(std::exchange(other.mTexture, nullptr))
        , mWidth(other.mWidth)
        , mHeight(other.mHeight)
        , mFormat(other.mFormat)
    {
    }

    Picture& Picture::operator=(Picture&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (mTexture != nullptr)
            MyGUI::RenderManager::getInstance().destroyTexture(mTexture);

        mName = std::move(other.mName);
        mTexture = std::exchange(other.mTexture, nullptr);
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mFormat = other.mFormat;

        return *this;
    }

    void Picture::set(const osg::Image& image)
    {
        const std::optional<MyGUI::PixelFormat> direct = directFormat(image);
        const bool wholeRows = direct.has_value()
            && image.getTotalSizeInBytes() == static_cast<std::size_t>(image.s()) * image.t() * bytesPerPixel(*direct);
        const MyGUI::PixelFormat format = wholeRows ? *direct : MyGUI::PixelFormat::R8G8B8A8;

        if (mTexture == nullptr || mWidth != image.s() || mHeight != image.t() || mFormat != format)
        {
            // MyGUI keys its textures by name and remaking one under a name it already knows
            // replaces it in place, so anything already showing this picture keeps pointing at it.
            mTexture = MyGUI::RenderManager::getInstance().createTexture(mName);
            mTexture->createManual(
                image.s(), image.t(), MyGUI::TextureUsage::Static | MyGUI::TextureUsage::Write, format);

            mWidth = image.s();
            mHeight = image.t();
            mFormat = format;
        }

        auto* pixels = static_cast<std::uint8_t*>(mTexture->lock(MyGUI::TextureUsage::Write));

        if (wholeRows)
            std::memcpy(pixels, image.data(), image.getTotalSizeInBytes());
        else
            for (int y = 0; y < mHeight; ++y)
                for (int x = 0; x < mWidth; ++x, pixels += 4)
                {
                    const osg::Vec4f colour = image.getColor(x, y);
                    pixels[0] = channel(colour.r());
                    pixels[1] = channel(colour.g());
                    pixels[2] = channel(colour.b());
                    pixels[3] = channel(colour.a());
                }

        mTexture->unlock();
    }

}
