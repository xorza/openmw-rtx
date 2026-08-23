#include "pixels.hpp"

#include <algorithm>
#include <cstring>

#include <osg/Image>

namespace MyGUIPlatform
{
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

    void writeRgba(const osg::Image& image, std::uint8_t* into)
    {
        const std::size_t count = static_cast<std::size_t>(image.s()) * image.t();

        if (directFormat(image) == MyGUI::PixelFormat::R8G8B8A8 && image.getTotalSizeInBytes() == count * 4)
        {
            std::memcpy(into, image.data(), count * 4);
            return;
        }

        for (int y = 0; y < image.t(); ++y)
            for (int x = 0; x < image.s(); ++x, into += 4)
            {
                const osg::Vec4f colour = image.getColor(x, y);
                into[0] = static_cast<std::uint8_t>(std::clamp(colour.r(), 0.f, 1.f) * 255.f + 0.5f);
                into[1] = static_cast<std::uint8_t>(std::clamp(colour.g(), 0.f, 1.f) * 255.f + 0.5f);
                into[2] = static_cast<std::uint8_t>(std::clamp(colour.b(), 0.f, 1.f) * 255.f + 0.5f);
                into[3] = static_cast<std::uint8_t>(std::clamp(colour.a(), 0.f, 1.f) * 255.f + 0.5f);
            }
    }
}
