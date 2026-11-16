#pragma once

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

#include "resourcemanager.hpp"

namespace osgDB
{
    class Options;
}

namespace Resource
{

    /// @brief Handles loading/caching of Images.
    /// @note May be used from any thread.
    class ImageManager : public ResourceManager<osg::ref_ptr<osg::Image>>
    {
    public:
        explicit ImageManager(const VFS::Manager* vfs, CacheBudget& budget);
        ~ImageManager() override;

        /// Create or retrieve an Image
        /// Returns the dummy image if the given image is not found.
        osg::ref_ptr<osg::Image> getImage(VFS::Path::NormalizedView path, bool disableFlip = false);

        osg::Image* getWarningImage();

        void reportStats(unsigned int frameNumber, osg::Stats* stats) const override;

    private:
        /// The image at `path` read from the files, or the warning image where none reads: `getImage`'s one load.
        osg::ref_ptr<osg::Image> readImage(VFS::Path::NormalizedView path, bool disableFlip);

        osg::ref_ptr<osg::Image> mWarningImage;
        osg::ref_ptr<osgDB::Options> mOptions;

        ImageManager(const ImageManager&);
        void operator=(const ImageManager&);
    };

}
