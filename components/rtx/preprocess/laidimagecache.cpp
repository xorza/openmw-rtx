#include "laidimagecache.hpp"

#include <string_view>

#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/sceneutil/embeddedimage.hpp>

namespace Rtx
{
    osg::ref_ptr<const osg::Image> LaidImageCache::of(const osg::Image& image)
    {
        const TextureFormat format = readFormat(image);
        const std::string_view name = SceneUtil::EmbeddedImage::nameOf(image);
        if (name.empty() || !isWidened(format))
            return nullptr;

        mName.assign(name);
        VFS::Path::normalizeFilenameInPlace(mName);
        auto known = mByFile.find(std::string_view(mName));
        if (known == mByFile.end())
            known = mByFile.emplace(VFS::Path::Normalized(mName), Laid{}).first;

        Laid& laid = known->second;
        osg::ref_ptr<const osg::Image> held;
        if (laid.mFile.get() == &image && laid.mLaid.lock(held))
            return held;

        held = layImage(image, format, mLevels, mTexels);
        laid.mFile = &image;
        laid.mLaid = held;
        return held;
    }
}
