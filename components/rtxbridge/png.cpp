#include "png.hpp"

#include <cstring>

#include <osg/Image>
#include <osgDB/WriteFile>

#include <components/files/conversion.hpp>
#include <components/rtx/error.hpp>

namespace RtxBridge
{
    void writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
        std::span<const std::uint8_t> pixels)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(static_cast<int>(width), static_cast<int>(height), 1, GL_RGBA, GL_UNSIGNED_BYTE);

        const std::size_t stride = std::size_t{ width } * 4;
        for (std::uint32_t y = 0; y < height; ++y)
            std::memcpy(image->data(0, static_cast<int>(height - 1 - y)), pixels.data() + y * stride, stride);

        if (!osgDB::writeImageFile(*image, Files::pathToUnicodeString(path)))
            throw Rtx::Error("cannot write " + Files::pathToUnicodeString(path));
    }
}
