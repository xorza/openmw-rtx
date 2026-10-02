#include "imagefactcache.hpp"

#include <cassert>
#include <string_view>

#include <osg/Image>

#include "contentpreprocessor.hpp"

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

    bool ImageFactCache::reachesSolid(ImageFacts& facts, const osg::Image& image)
    {
        if (!facts.mReachesSolid.has_value())
            facts.mReachesSolid = mContent.reachesSolid(image);
        return *facts.mReachesSolid;
    }

    const MeanTexel& ImageFactCache::meanOf(ImageFacts& facts, const osg::Image& image)
    {
        if (!facts.mMean.has_value())
            facts.mMean = mContent.meanTexel(image);
        return *facts.mMean;
    }
}
