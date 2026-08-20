#include "texturebuilder.hpp"

#include <algorithm>

#include <osg/Image>

#include <components/debug/debuglog.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/scenedesc.hpp>

namespace RtxBridge
{
    namespace
    {
        /// sRGB throughout, because that is what the files hold.
        ///
        /// Morrowind's textures were authored and stored display-encoded. Sampling them as sRGB is
        /// what hands the shader linear values, which is the only thing light transport can be done
        /// in — and it is free, because the hardware does the conversion in the filter.
        VkFormat toVulkanFormat(GLenum pixelFormat)
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
                    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
                    return VK_FORMAT_BC2_SRGB_BLOCK;
                case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
                    return VK_FORMAT_BC3_SRGB_BLOCK;
                default:
                    return VK_FORMAT_UNDEFINED;
            }
        }
    }

    Rtx::TextureData describeImage(const osg::Image& image, std::vector<Rtx::MipLevel>& levels)
    {
        const VkFormat format = toVulkanFormat(image.getPixelFormat());
        if (format == VK_FORMAT_UNDEFINED)
            throw Rtx::Error("texture \"" + image.getFileName() + "\" is pixel format "
                + std::to_string(image.getPixelFormat())
                + ", and this renderer uploads only the block-compressed formats Morrowind ships");

        const auto width = static_cast<std::uint32_t>(image.s());
        const auto height = static_cast<std::uint32_t>(image.t());

        levels.clear();
        const unsigned int count = image.getNumMipmapLevels();
        levels.reserve(count);
        for (unsigned int level = 0; level < count; ++level)
            levels.push_back(Rtx::MipLevel{
                .mOffset = image.getMipmapOffset(level),
                .mWidth = std::max(width >> level, 1u),
                .mHeight = std::max(height >> level, 1u),
            });

        return Rtx::TextureData{
            .mFormat = format,
            .mWidth = width,
            .mHeight = height,
            .mBytes
            = std::span(reinterpret_cast<const std::byte*>(image.data()), image.getTotalSizeInBytesIncludingMipmaps()),
            .mLevels = levels,
        };
    }

    Rtx::TextureArray buildTextures(
        const Rtx::Device& device, Rtx::CommandPool& pool, const Rtx::SceneDesc& scene, Resource::ImageManager& images)
    {
        std::vector<Rtx::Texture> textures;
        textures.reserve(scene.getTextures().size());

        // Refilled per texture rather than reallocated, and it has to outlive the description that
        // points into it, which is why it is here and not inside the loop.
        std::vector<Rtx::MipLevel> levels;

        for (const VFS::Path::Normalized& path : scene.getTextures())
        {
            // Already decoded and still resident: the scene manager keeps image data on the CPU
            // after apply, so this is a cache hit and a memcpy rather than a second decode.
            const osg::ref_ptr<osg::Image> image = images.getImage(path);
            textures.emplace_back(device, pool, describeImage(*image, levels), path.value());
        }

        return Rtx::TextureArray(device, std::move(textures));
    }
}
