#include "imagefactcache.hpp"

#include <cassert>
#include <string_view>

#include <osg/Image>

#include <components/sceneutil/embeddedimage.hpp>

namespace Rtx
{
    ImageFacts& ImageFactCache::of(const osg::Image& image)
    {
        const std::string_view name = SceneUtil::EmbeddedImage::nameOf(image);
        assert(!name.empty() && "the facts of an image the texture table would have refused");

        // Normalised as the texture table normalises it, so one file under two spellings is one
        // entry.
        mName.assign(name);
        VFS::Path::normalizeFilenameInPlace(mName);
        if (const auto known = mByFile.find(std::string_view(mName)); known != mByFile.end())
            return known->second;

        return mByFile.emplace(VFS::Path::Normalized(mName), ImageFacts{}).first->second;
    }
}
