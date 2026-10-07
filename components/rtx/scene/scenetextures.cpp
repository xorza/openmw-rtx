#include "scenetextures.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/mipchain.hpp>
#include <components/vfs/pathutil.hpp>

#include "refusal.hpp"
#include "scenedesc.hpp"
#include "texturetable.hpp"

namespace Rtx
{
    void SceneTextures::describeAll(const SceneDesc& scene)
    {
        describe(scene, everyIndexBelow(scene.textures().getRows().size(), mEverything));
    }

    void SceneTextures::describe(const SceneDesc& scene, std::span<const Index> slots)
    {
        mLevels.clear();
        mTexels.clear();
        mDescriptions.clear();
        mKept.clear();
        mRefusals.clear();

        mKept.reserve(slots.size());

        for (const Index slot : slots)
        {
            // A free slot is not a texture. `SceneDesc` empties one the last thing naming it
            // gave back and leaves it in the table until something takes it over; describing it
            // would build an image, a shading map and a descriptor write for a slot no material can
            // reach — and count it as a texture that arrived.
            if (!scene.textures().isLive(slot))
                continue;

            // A slot this renderer made rather than opened has no file to be asked for, and the
            // entry still has to exist because the description below is built from it.
            const TextureRow& row = scene.textures().getRows()[slot];
            Kept kept{ .mSlot = slot, .mEncoding = row.mEncoding };

            // The image the adder held, and never the file opened again: this runs on the frame an
            // arrival lands on, and a path is a lock and a disk read.
            if (row.mKind == TextureKind::File && row.mImage != nullptr)
            {
                kept.mImage = row.mImage;
                kept.mFormat = row.mFormat;
            }
            else if (row.mKind == TextureKind::File)
                kept.mImage = Misc::Err{ std::string(sNoImage) };
            else if (row.mKind == TextureKind::SpriteLight)
            {
                // Made on the device from the sprite texture's own slot, which the emitter holds
                // beside this one: a bake carries no bytes and is shaped like its source there.
                kept.mBakedFrom = scene.textures().findFile(row.mPath);
            }
            else
                kept.mGround = Ground{ .mMaterial = row.mGroundOf, .mGloss = row.mKind == TextureKind::GroundGloss };

            mKept.push_back(std::move(kept));
        }

        // Reserved before anything points into it, and that is what makes the spans safe. Every
        // description spans this one table, so it must not grow while they are being taken — and
        // every level count is known before the first description is built. A table kept from the
        // last arrival is usually large enough already, and then this asks for nothing. Only a file
        // puts levels here: a bake and a composite carry none, and the stand-in's are its own.
        std::size_t levels = 0;
        std::size_t texels = 0;
        for (const Kept& kept : mKept)
            if (kept.mImage.isOk() && kept.mImage.value() != nullptr)
            {
                levels += kept.mImage.value()->getNumMipmapLevels();
                texels += laidBytes(*kept.mImage.value(), kept.mFormat);
            }
        mLevels.reserve(levels);
        mTexels.reserve(texels);

        // What the assertions below are taken against: the reserve and the fill agree by argument
        // through branches that push a different number of levels each, and a growth is the failure.
        [[maybe_unused]] const std::size_t reserved = mLevels.capacity();
        [[maybe_unused]] const std::size_t reservedTexels = mTexels.capacity();

        mDescriptions.reserve(mKept.size());
        for (const Kept& kept : mKept)
        {
            const TextureRow& row = scene.textures().getRows()[kept.mSlot];

            const Misc::Result<TextureData, std::string> described = describeKept(kept);
            TextureData data;
            if (described.isOk())
                data = described.value();
            else
            {
                mRefusals.push_back(
                    Refusal{ .mKind = Refused::Texture, .mName = row.getName(), .mWhy = described.error() });
                data = describeStandIn();
            }

            data.mSlot = kept.mSlot;
            data.mWrap = row.mWrap;
            mDescriptions.push_back(data);
        }

        assert(mLevels.capacity() == reserved && "the level table grew while descriptions spanned it");
        assert(mTexels.capacity() == reservedTexels && "the laid texels grew while descriptions spanned them");
    }

    Misc::Result<TextureData, std::string> SceneTextures::describeKept(const Kept& kept)
    {
        if (!kept.mImage.isOk())
            return Misc::Err{ kept.mImage.error() };

        if (const osg::Image* image = kept.mImage.value().get())
        {
            const Misc::Result<TextureData, std::string> read
                = describeImage(*image, kept.mFormat, kept.mEncoding, mLevels, mTexels);
            if (!read.isOk())
                return read;

            // What the file did not carry, the device makes. `MipChain` says why almost nothing in
            // the game needs this and why the rain does.
            TextureData described = read.value();
            described.mCompleteChain = MipChain::wantedFor(described);
            return described;
        }

        if (kept.mBakedFrom.has_value())
        {
            // A source the table no longer holds is a bake of nothing.
            if (*kept.mBakedFrom == sNoIndex)
                return Misc::Err{ "the texture it bakes is no longer held" };

            return TextureData{
                .mSource = TextureSource::SpriteBake,
                .mFrom = *kept.mBakedFrom,
                .mFormat = TextureFormat::Rgba8Unorm,
                .mEncoding = kept.mEncoding,
            };
        }

        // Flattened on the device in the placement after this arrival, from the chunk's own stack,
        // which the slot's row names however long ago it was made: the description carries the
        // chunk and no bytes.
        assert(kept.mGround.has_value() && "a slot with no file and no bake is a chunk's ground");
        const Ground& chunk = *kept.mGround;
        if (chunk.mGloss)
            return TextureData{
                .mSource = TextureSource::GroundGloss,
                .mFrom = chunk.mMaterial,
                .mFormat = TextureFormat::Rgba8Unorm,
                .mEncoding = kept.mEncoding,
            };

        return TextureData{
            .mSource = TextureSource::GroundComposite,
            .mFrom = chunk.mMaterial,
            .mFormat = TextureFormat::Rgba8Srgb,
            .mEncoding = kept.mEncoding,
        };
    }
}
