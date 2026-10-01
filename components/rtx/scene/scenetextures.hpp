#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/misc/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>

#include "refusal.hpp"

namespace Rtx
{
    class CompositeQueue;
    class SceneDesc;

    /// Every live texture a scene names, described, and the storage those descriptions point into.
    /// Each description carries the slot it belongs to and there is not one per slot: a slot the
    /// scene has given back is passed over. `TextureData` carries spans rather than bytes, so this
    /// holds the images and owns the level table while a backend reads them, and knows no
    /// graphics API. Non-copyable because the descriptions point into its own vectors; held for
    /// the life of its owner and refilled per arrival, so every buffer settles at the busiest cell.
    class SceneTextures
    {
    public:
        SceneTextures() = default;

        SceneTextures(const SceneTextures&) = delete;
        SceneTextures& operator=(const SceneTextures&) = delete;
        SceneTextures(SceneTextures&&) = delete;
        SceneTextures& operator=(SceneTextures&&) = delete;

        /// Resolves and describes every texture `scene` still names, in table order, for a backend
        /// building an array from nothing. The free slots are not among them. A texture that cannot
        /// be described is described as the stand-in and refused, and so is the array running out
        /// of room.
        /// @param composites which slots are chunks' flattened ground, or null for a caller that
        ///        flattens none. A terrain slot the queue did not give out is described as the
        ///        stand-in and refused.
        void describeAll(const SceneDesc& scene, const CompositeQueue* composites = nullptr);

        /// The same, for `slots` and nothing else — what stops a texture being described twice. A
        /// list and not an offset, because a slot a departing cell freed is taken over wherever it
        /// sits.
        void describe(const SceneDesc& scene, std::span<const Index> slots, const CompositeQueue* composites = nullptr);

        /// What the last `describe` found, each carrying the slot it goes to in `TextureData::mSlot`.
        std::span<const TextureData> getDescriptions() const { return mDescriptions; }

        /// What the last `describe` refused, for whoever owns the scene to report: the scene is
        /// read here, because a read-only caller describes it too.
        std::span<const Refusal> getRefusals() const { return mRefusals; }

    private:
        /// One slot `describe` decided to describe, and what resolving it found.
        struct Kept
        {
            Index mSlot = sNoIndex;

            /// What the scene's table binds the slot as, which the file's bytes are read as.
            TextureEncoding mEncoding = TextureEncoding::Colour;

            /// For a bake, the slot of the sprite texture it is made from on the device, or
            /// `sNoIndex` where the table no longer holds that. Nothing for a slot that is no bake.
            std::optional<Index> mBakedFrom{};

            /// The file's image, or why none reads. Null for a slot that names no file.
            Misc::Result<osg::ref_ptr<const osg::Image>, std::string> mImage = osg::ref_ptr<const osg::Image>();

            /// The image's format as `mEncoding` reads it, read once for the reserve and the
            /// description both. Unnamed where there is no image.
            TextureFormat mFormat = TextureFormat::Unnamed;
        };

        /// What `kept` is described as, or why it gets the stand-in.
        Misc::Result<TextureData, std::string> describeKept(const Kept& kept, const CompositeQueue* composites);

        // Refilled by every `describe` and never freed, so each settles at the busiest arrival so
        // far — which is where the room to grow one is least.

        /// The slots `describe` kept, because a free one is passed over and the descriptions are
        /// no longer one per entry of what it was asked for.
        std::vector<Kept> mKept;

        /// Every image's levels, back to back. One table rather than one vector each: a cell reaches
        /// a couple of hundred textures, and the descriptions want a span into something stable.
        std::vector<MipLevel> mLevels;

        /// Every laid image's texels, back to back, for the same reason.
        std::vector<std::byte> mTexels;

        std::vector<TextureData> mDescriptions;

        /// Every slot of the scene's table, which is what a rebuild asks about. Held rather than
        /// built, because a rebuild is a fifth of a second and none of it should be this.
        std::vector<Index> mEverything;

        std::vector<Refusal> mRefusals;
    };
}
