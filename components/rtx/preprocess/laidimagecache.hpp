#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <osg/Image>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <components/rtx/image/texturedata.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    /// Every file of a widened format this thread laid as the upload reads it (`layImage`), found
    /// again by its file name while anything still holds the laid image. **Watched and not held**:
    /// a laid image is four bytes a texel beside the file, a 2048 by 2048 TGA 22 megabytes, and one
    /// kept for the session past the last texture naming it would be all of them at once. The file
    /// is watched too, so a name the loader opened again — another object under the same name —
    /// is laid again rather than answered with what another image laid. One a thread, in its
    /// `ThreadContent`: the ring's reader lays what the frame's hand-over then reads as it stands.
    class LaidImageCache
    {
    public:
        /// `image` laid, or null where its format is not widened, where it has no name, or where
        /// `layImage` refuses it. Lays it only where nothing holds what it laid before.
        osg::ref_ptr<const osg::Image> of(const osg::Image& image);

        std::size_t size() const { return mByFile.size(); }

    private:
        struct Laid
        {
            osg::observer_ptr<const osg::Image> mFile;
            osg::observer_ptr<const osg::Image> mLaid;
        };

        std::unordered_map<VFS::Path::Normalized, Laid, VFS::Path::Hash, std::equal_to<>> mByFile;

        /// The file name being looked for, normalised in place.
        std::string mName;

        /// `layImage`'s scratch, refilled for each file laid.
        std::vector<MipLevel> mLevels;
        std::vector<std::byte> mTexels;
    };
}
