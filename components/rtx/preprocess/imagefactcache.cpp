#include "imagefactcache.hpp"

#include <cassert>
#include <string_view>

#include <osg/Image>

namespace Rtx
{
    ImageFacts& ImageFactCache::of(const osg::Image& image)
    {
        assert(!image.getFileName().empty() && "the facts of an image the texture table would have refused");

        // Normalised as the texture table normalises it, so one file under two spellings is one
        // entry.
        mName.assign(image.getFileName());
        VFS::Path::normalizeFilenameInPlace(mName);
        if (const auto known = mByFile.find(std::string_view(mName)); known != mByFile.end())
            return known->second;

        return mByFile.emplace(VFS::Path::Normalized(mName), ImageFacts{}).first->second;
    }
}
