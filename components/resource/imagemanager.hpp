#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

#include "imageheader.hpp"
#include "resourcemanager.hpp"

namespace osgDB
{
    class Options;
}

namespace Resource
{
    /// What a loaded model's textures hold of their files.
    enum class ModelImages
    {
        /// The texels, which the rasterizer uploads from the graph.
        Texels,

        /// The header alone (`ImageHeader`): for a renderer that reads a file's texels itself and keeps them only
        /// until the device has its copy, so a model kept in the cache keeps none.
        Headers,
    };

    /// @brief Handles loading/caching of Images.
    /// @note May be used from any thread.
    class ImageManager : public ResourceManager<osg::ref_ptr<osg::Image>>
    {
    public:
        ImageManager(const ImageManager&) = delete;
        void operator=(const ImageManager&) = delete;
        explicit ImageManager(const VFS::Manager* vfs, CacheBudget& budget);
        ~ImageManager() override;

        /// Create or retrieve an Image
        /// Returns the dummy image if the given image is not found.
        osg::ref_ptr<osg::Image> getImage(VFS::Path::NormalizedView path, bool disableFlip = false);

        osg::Image* getWarningImage();

        /// Before any model is loaded, which every loader thread reads it for: headers with what derives each where
        /// its file is read, and no deriver for texels, which the rasterizer reads from the graph.
        void setModelImages(ModelImages images, HeaderDeriver deriver);

        /// What a model's texture at `path` holds, as `setModelImages` says: the image `getImage` hands out, or its
        /// header, the file read the first time a model names it and never again for a model. What every loader of
        /// a model's textures asks.
        osg::ref_ptr<osg::Image> getModelImage(VFS::Path::NormalizedView path);

        void reportStats(std::uint32_t frameNumber, osg::Stats* stats) const override;

    private:
        /// The image at `path` read from the files, or the warning image where none reads: `getImage`'s one load.
        osg::ref_ptr<osg::Image> readImage(VFS::Path::NormalizedView path, bool disableFlip);

        osg::ref_ptr<osg::Image> mWarningImage;
        osg::ref_ptr<osgDB::Options> mOptions;

        ModelImages mModelImages = ModelImages::Texels;
        HeaderDeriver mDeriver;

        /// Every header made, by its file: tiny, and bounded by the files the content names, one each, so that
        /// every texture of one file names one header, as it named one image.
        std::mutex mHeadersMutex;
        std::unordered_map<VFS::Path::Normalized, osg::ref_ptr<osg::Image>, VFS::Path::Hash, std::equal_to<>> mHeaders;
    };

}
