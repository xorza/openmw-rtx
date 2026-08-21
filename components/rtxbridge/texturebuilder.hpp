#pragma once

#include <span>
#include <vector>

#include <osg/Image>

#include <components/rtx/texturedata.hpp>

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    class SceneDesc;
}

namespace RtxBridge
{
    /// Describes one image for a backend's uploader without copying a byte of it.
    ///
    /// Levels are **appended** to `levels`, and the returned description spans the ones it added —
    /// so `levels` must outlive the upload and must not grow again while the description is alive.
    /// `SceneTextures` is what reserves for that.
    ///
    /// Throws for a format Morrowind does not produce: every texture in the game is block
    /// compressed with its mip chain already built, and inventing a conversion path for something no
    /// content file contains is how a renderer grows code nothing runs.
    Rtx::TextureData describeImage(const osg::Image& image, std::vector<Rtx::MipLevel>& levels);

    /// Every texture a scene names, described in the order it names them, and the storage those
    /// descriptions point into.
    ///
    /// **This is where the bridge stops.** `Rtx::TextureData` carries spans rather than bytes, so
    /// something has to own the decoded images and the level table while a backend reads them; this
    /// is that, and it knows no graphics API. Which of them becomes a `VkImage` or an `MTLTexture`
    /// is the backend's business and none of this one's.
    ///
    /// Non-copyable because the descriptions point into its own vectors. Moving is fine: a moved
    /// vector keeps the buffer they point at.
    class SceneTextures
    {
    public:
        /// Resolves and describes every texture `scene` names.
        SceneTextures(const Rtx::SceneDesc& scene, Resource::ImageManager& images);

        SceneTextures(const SceneTextures&) = delete;
        SceneTextures& operator=(const SceneTextures&) = delete;
        SceneTextures(SceneTextures&&) = default;
        SceneTextures& operator=(SceneTextures&&) = default;

        /// Indexed by the scene's texture index, which is what a material stores.
        std::span<const Rtx::TextureData> getDescriptions() const { return mDescriptions; }

        /// How many of them could not be read and got the stand-in instead.
        ///
        /// **Not zero in the game.** The harness names textures out of content files and every one
        /// of them is a `.dds` on disk; a live scene graph also holds textures that were never files
        /// — a terrain composite map rendered on the GPU, a render-to-texture target, something a
        /// script made. Those have no bytes to upload and no business bringing the renderer down.
        std::uint32_t getUnreadable() const { return mUnreadable; }

    private:
        std::vector<osg::ref_ptr<const osg::Image>> mImages;

        /// Every texture's estimated lighting, back to back and `SHADING_EXTENT` squared apiece.
        ///
        /// **Made on load and thrown away with the cell**, because a cache would cost more than it
        /// saved: a cell's couple of hundred textures estimate in well under a millisecond.
        std::vector<float> mShading;

        /// Every image's levels, back to back. One table rather than one vector each: a cell reaches
        /// a couple of hundred textures, and the descriptions want a span into something stable.
        std::vector<Rtx::MipLevel> mLevels;

        std::vector<Rtx::TextureData> mDescriptions;
        std::uint32_t mUnreadable = 0;
    };
}
