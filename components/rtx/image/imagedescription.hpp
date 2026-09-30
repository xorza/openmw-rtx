#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/common/result.hpp>
#include <components/vfs/pathutil.hpp>

#include "texturedata.hpp"
#include "textureencoding.hpp"

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    /// Why a file has nothing to upload: no image reads from it. What `openImage` answers, and what
    /// a table slot whose adder found no image is refused with.
    inline constexpr std::string_view sNoImage = "no image reads from the file";

    /// What a texture that cannot stand is drawn as, described: one texel block of mid grey, in
    /// storage of its own that lasts the program. The one definition, which a backend stands once
    /// for every slot that draws it and a contact sheet draws as it is.
    TextureData describeStandIn();

    /// Whether this renderer uploads `image` as `encoding`, and why not where it does not: a format
    /// Morrowind does not produce, or an image of no size or no texels. The name is left to
    /// whoever reports it.
    Result<void, std::string> checkUploadable(
        const osg::Image& image, TextureEncoding encoding = TextureEncoding::Colour);

    /// How many bytes `describeImage` lays into its `texels` for `image`, which is what a
    /// caller holding earlier descriptions reserves first: a sixteen-bit format widened, or a
    /// volume's first slices gathered. Nought for an image spanned where it is, and for one it
    /// refuses, whose format may have no layout to count by.
    std::size_t laidBytes(const osg::Image& image, TextureFormat format);

    /// Describes one image for a backend's uploader: the first slice of each level, spanned where
    /// the image holds them back to back in a format uploaded as it is, and laid into `texels`
    /// where it does not — widened from sixteen bits a texel, or gathered from a volume's levels.
    /// Levels are appended to `levels` and laid texels to `texels`, and the description spans what
    /// it added, so neither may grow again while it is alive. The levels are the file's own; a
    /// backend completes a chain the file did not carry, on the device. An error, adding nothing,
    /// where `checkUploadable` answers one, or where the format's layout and OpenSceneGraph's
    /// count the image's bytes differently.
    Result<TextureData, std::string> describeImage(const osg::Image& image, std::vector<MipLevel>& levels,
        std::vector<std::byte>& texels, TextureEncoding encoding = TextureEncoding::Colour);

    /// `describeImage` of an image whose format the caller has read already, as `encoding`
    /// (`readFormat`).
    Result<TextureData, std::string> describeImage(const osg::Image& image, TextureFormat format,
        TextureEncoding encoding, std::vector<MipLevel>& levels, std::vector<std::byte>& texels);

    /// `describeImage` of the finest level alone, as a colour, for a reader of nothing else: the
    /// coarser levels are neither held against OpenSceneGraph's count nor laid, and a volume's first
    /// slice of it is spanned where it lies.
    Result<TextureData, std::string> describeFinestLevel(
        const osg::Image& image, std::vector<MipLevel>& levels, std::vector<std::byte>& texels);

    /// The image at `path`, or why nothing reads there — an error and not an exception, because a
    /// live scene graph names textures that were never files and a renderer that fell over on one
    /// would fall over on a cell. Never null.
    Result<osg::ref_ptr<const osg::Image>, std::string> openImage(
        Resource::ImageManager& images, VFS::Path::NormalizedView path);
}
