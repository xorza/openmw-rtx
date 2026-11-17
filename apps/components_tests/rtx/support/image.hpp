#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

namespace Rtx::Testing
{
    /// An image of `width` by `height` texels in `format` and `type`, one deep, every byte nought, under the name
    /// `file` a loader would give it: what a test of a reader hands it where a file would arrive.
    inline osg::ref_ptr<osg::Image> makeImage(std::string_view file, std::int32_t width, std::int32_t height,
        GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->setFileName(std::string(file));
        image->allocateImage(width, height, 1, format, type);
        std::fill_n(image->data(), image->getTotalSizeInBytes(), static_cast<std::uint8_t>(0));
        return image;
    }
}
