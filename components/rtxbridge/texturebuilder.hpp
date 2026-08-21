#pragma once

#include <vector>

#include <components/rtxvulkan/texture.hpp>

namespace osg
{
    class Image;
}

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    class CommandPool;
    class Device;
    class SceneDesc;
}

namespace RtxBridge
{
    /// Describes an image for the uploader without copying a byte of it.
    ///
    /// `levels` is filled in and the returned description points into it and into the image, so both
    /// must outlive the upload. Throws for a format Morrowind does not produce — every texture in
    /// the game is BC1 or BC2 with its mip chain already built, and inventing a conversion path for
    /// something no content file contains is how a renderer grows code nothing runs.
    Rtx::TextureData describeImage(const osg::Image& image, std::vector<Rtx::MipLevel>& levels);

    /// Uploads every texture the scene names, in the order it names them, so a material's texture
    /// index is an index into the returned array.
    Rtx::TextureArray buildTextures(
        const Rtx::Device& device, Rtx::CommandPool& pool, const Rtx::SceneDesc& scene, Resource::ImageManager& images);
}
