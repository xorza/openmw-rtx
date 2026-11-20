#include "imageheader.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/vfs/pathutil.hpp>

#include "imagemanager.hpp"

namespace Resource
{
    HeaderShape shapeOf(const osg::Image& image)
    {
        return HeaderShape{ .mWidth = image.s(),
            .mHeight = image.t(),
            .mDepth = image.r(),
            .mInternalFormat = image.getInternalTextureFormat(),
            .mPixelFormat = image.getPixelFormat(),
            .mDataType = image.getDataType(),
            .mPacking = image.getPacking(),
            .mRowLength = image.getRowLength(),
            .mOrigin = image.getOrigin(),
            .mMipmaps = image.getMipmapLevels() };
    }

    ImageHeader::ImageHeader(const HeaderShape& shape, const std::string& fileName, ImageManager& source,
        osg::ref_ptr<const osg::Referenced> derived)
        : mSource(&source)
        , mDerived(std::move(derived))
    {
        assert(!fileName.empty() && "a header of an image no file names, which nothing can read again");
        setFileName(fileName);
        // No texels, and nothing to delete: the size, the format and the levels' offsets describe the file's.
        setImage(shape.mWidth, shape.mHeight, shape.mDepth, shape.mInternalFormat, shape.mPixelFormat, shape.mDataType,
            nullptr, osg::Image::NO_DELETE, static_cast<std::int32_t>(shape.mPacking), shape.mRowLength);
        setMipmapLevels(shape.mMipmaps);
        setOrigin(shape.mOrigin);
    }

    ImageHeader::ImageHeader(const ImageHeader& copy, const osg::CopyOp& op)
        : osg::Image(copy, op)
        , mSource(copy.mSource)
        , mDerived(copy.mDerived)
    {
    }

    osg::ref_ptr<const osg::Image> ImageHeader::readTexels() const
    {
        assert(mSource != nullptr && "a header made with no manager to read its texels");
        return mSource->getImage(VFS::Path::Normalized(std::string_view(getFileName())));
    }

    void ImageHeader::releaseTexels() const
    {
        assert(mSource != nullptr && "a header made with no manager to read its texels");
        mSource->removeFromCache(VFS::Path::Normalized(std::string_view(getFileName())).value());
    }

    bool isHeader(const osg::Image& image)
    {
        return dynamic_cast<const ImageHeader*>(&image) != nullptr;
    }

    osg::ref_ptr<const osg::Image> texelsOf(const osg::Image& image)
    {
        if (const auto* const header = dynamic_cast<const ImageHeader*>(&image))
            return header->readTexels();
        return &image;
    }

    void releaseTexels(const osg::Image& image)
    {
        if (const auto* const header = dynamic_cast<const ImageHeader*>(&image))
            header->releaseTexels();
    }
}
