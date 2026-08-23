#include "picture.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>

#include <osg/Image>

#include "pixels.hpp"

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
            writeRgba(image, pixels);

        mTexture->unlock();
    }

}
