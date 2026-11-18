#include "imageheader.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>

#include <components/vfs/pathutil.hpp>

#include "imagemanager.hpp"

namespace Resource
{
    ImageHeader::ImageHeader(const osg::Image& pixels, ImageManager& source)
        : mSource(&source)
    {
        assert(!pixels.getFileName().empty() && "a header of an image no file names, which nothing can read again");
        setFileName(pixels.getFileName());
        // No texels, and nothing to delete: the size, the format and the levels' offsets describe the file's.
        setImage(pixels.s(), pixels.t(), pixels.r(), pixels.getInternalTextureFormat(), pixels.getPixelFormat(),
            pixels.getDataType(), nullptr, osg::Image::NO_DELETE, static_cast<std::int32_t>(pixels.getPacking()),
            pixels.getRowLength());
        setMipmapLevels(pixels.getMipmapLevels());
        setOrigin(pixels.getOrigin());
    }

    ImageHeader::ImageHeader(const ImageHeader& copy, const osg::CopyOp& op)
        : osg::Image(copy, op)
        , mSource(copy.mSource)
    {
        // What was derived of the texels is the file's, which the copy names too.
        const std::scoped_lock lock(copy.mDerivedMutex);
        mDerived = copy.mDerived;
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
