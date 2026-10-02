#include "imagefactcache.hpp"

#include <cassert>
#include <utility>

#include <osg/Image>

#include "contentpreprocessor.hpp"

namespace Rtx
{
    const ImageFacts& ImageFactCache::of(const osg::Image& image)
    {
        assert(!image.getFileName().empty() && "the facts of an image the texture table would have refused");

        // Normalised as the texture table normalises it, so one file under two spellings is one
        // entry. The string is built once per image met and never per ask: a caller keeps the
        // reference, which this never invalidates.
        VFS::Path::Normalized file(image.getFileName());
        if (const auto known = mByFile.find(file); known != mByFile.end())
            return known->second;

        return mByFile.emplace(std::move(file), mContent.imageFacts(image)).first->second;
    }
}
