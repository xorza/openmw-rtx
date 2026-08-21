#include "texturebuilder.hpp"

#include <algorithm>
#include <optional>

#include <osg/Image>

#include <components/resource/imagemanager.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/scenedesc.hpp>

namespace RtxBridge
{
    namespace
    {
        /// What OpenSceneGraph decoded, as one of the formats this renderer uploads.
        ///
        /// Nothing where the file is something else, so the caller says so with the file's name in
        /// the message. `Rtx::TextureFormat` is why every case is sRGB.
        std::optional<Rtx::TextureFormat> toTextureFormat(GLenum pixelFormat)
        {
            switch (pixelFormat)
            {
                // Both DXT1 spellings land on the format that reads the alpha bit, and the header
                // that claims there is none is not consulted. A BC1 block is punch-through whenever
                // its first endpoint sorts below its second, which is how every mask in the game is
                // stored; almost none of Morrowind's files set `DDPF_ALPHAPIXELS`, so believing the
                // header would decode that bit as opaque black and leave every canopy a solid card.
                // The bytes are identical either way — this only chooses whether the bit is looked at.
                case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
                case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
                    return Rtx::TextureFormat::Bc1RgbaSrgb;
                case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
                    return Rtx::TextureFormat::Bc2Srgb;
                case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
                    return Rtx::TextureFormat::Bc3Srgb;
                default:
                    return std::nullopt;
            }
        }
    }

    Rtx::TextureData describeImage(const osg::Image& image, std::vector<Rtx::MipLevel>& levels)
    {
        const std::optional<Rtx::TextureFormat> format = toTextureFormat(image.getPixelFormat());
        if (!format.has_value())
            throw Rtx::Error("texture \"" + image.getFileName() + "\" is pixel format "
                + std::to_string(image.getPixelFormat())
                + ", and this renderer uploads only the block-compressed formats Morrowind ships");

        const auto width = static_cast<std::uint32_t>(image.s());
        const auto height = static_cast<std::uint32_t>(image.t());

        const std::size_t first = levels.size();
        const unsigned int count = image.getNumMipmapLevels();
        for (unsigned int level = 0; level < count; ++level)
            levels.push_back(Rtx::MipLevel{
                .mOffset = image.getMipmapOffset(level),
                .mWidth = std::max(width >> level, 1u),
                .mHeight = std::max(height >> level, 1u),
            });

        return Rtx::TextureData{
            .mFormat = *format,
            .mWidth = width,
            .mHeight = height,
            .mBytes
            = std::span(reinterpret_cast<const std::byte*>(image.data()), image.getTotalSizeInBytesIncludingMipmaps()),
            .mLevels = std::span<const Rtx::MipLevel>(levels).subspan(first, count),
            .mName = image.getFileName(),
        };
    }

    SceneTextures::SceneTextures(const Rtx::SceneDesc& scene, Resource::ImageManager& images)
    {
        const std::span<const VFS::Path::Normalized> paths = scene.getTextures();

        mImages.reserve(paths.size());
        for (const VFS::Path::Normalized& path : paths)
            // Already decoded and still resident: the scene manager keeps image data on the CPU
            // after apply, so this is a cache hit and a memcpy rather than a second decode.
            mImages.push_back(images.getImage(path));

        // **Reserved exactly, and that is what makes the spans safe.** Every description points into
        // this one table, so it must not reallocate while they are being taken — and every level
        // count is known before the first description is built.
        std::size_t levels = 0;
        for (const osg::ref_ptr<const osg::Image>& image : mImages)
            levels += image->getNumMipmapLevels();
        mLevels.reserve(levels);

        mDescriptions.reserve(mImages.size());
        for (const osg::ref_ptr<const osg::Image>& image : mImages)
            mDescriptions.push_back(describeImage(*image, mLevels));
    }
}
