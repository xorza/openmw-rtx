#include "scenetextures.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "refusal.hpp"
#include "scenedesc.hpp"
#include "texturetable.hpp"

namespace Rtx
{
    namespace
    {
        /// The image `row`'s chain is made from, or null where nothing is to be made of it: a file
        /// whose made image its slot does not hold yet, of a kind the service makes
        /// (`PreparedImages::prepares`). Laid where a reader laid it.
        const osg::Image* toPrepare(const TextureRow& row)
        {
            const osg::Image* const source = row.mLaid != nullptr ? row.mLaid.get() : row.mImage.get();
            return row.mKind == TextureKind::File && row.mPrepared == nullptr && source != nullptr
                    && PreparedImages::prepares(*source, row.mEncoding)
                ? source
                : nullptr;
        }
    }

    void SceneTextures::describeAll(const SceneDesc& scene, PreparedImages* const prepared, const bool settled)
    {
        describe(scene, everyIndexBelow(scene.textures().getRows().size(), mEverything), prepared, settled);
    }

    void SceneTextures::describe(
        const SceneDesc& scene, std::span<const Index> slots, PreparedImages* const prepared, const bool settled)
    {
        mPending.clear();
        mMade.clear();

        // **Every image the slots need asked for first, and waited for**, where the caller is settled:
        // asked one at a time and each waited for, they would be made one after another.
        if (settled && prepared != nullptr)
        {
            for (const Index slot : slots)
            {
                if (!scene.textures().isLive(slot))
                    continue;
                const TextureRow& row = scene.textures().getRows()[slot];
                if (const osg::Image* const source = toPrepare(row))
                    prepared->find(*source, row.mEncoding);
            }
            prepared->flush();
        }
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
            // would build an image, a shading map and a binding for a slot no material can
            // reach — and count it as a texture that arrived.
            if (!scene.textures().isLive(slot))
                continue;

            // A slot this renderer made rather than opened has no file to be asked for, and the
            // entry still has to exist because the description below is built from it.
            const TextureRow& row = scene.textures().getRows()[slot];
            Kept kept{ .mSlot = slot, .mEncoding = row.mEncoding };

            // The image the adder held, and never the file opened again: this runs on the frame an
            // arrival lands on, and a path is a lock and a disk read. Laid where a reader laid it, so
            // its texels are read as they stand rather than widened here.
            // **A normal or a data map's image with its chain made again**, where it is made; its file
            // as it stands while it is, and the slot named for its owner to stand again.
            const osg::Image* const source = toPrepare(row);
            const PreparedImage made = source != nullptr && prepared != nullptr
                ? prepared->find(*source, row.mEncoding)
                : PreparedImage{ .mImage = row.mPrepared };
            if (made.mPending)
                mPending.push_back(Pending{ .mSlot = slot, .mSource = source, .mEncoding = row.mEncoding });
            if (made.mImage != nullptr && row.mPrepared == nullptr)
                mMade.push_back(Made{ .mSlot = slot, .mImage = made.mImage });

            if (made.mImage != nullptr)
            {
                const osg::Image& file = row.mLaid != nullptr ? *row.mLaid : *row.mImage;
                kept.mImage = osg::ref_ptr<const osg::Image>(&file);
                kept.mFormat = readFormat(file, row.mEncoding);
                kept.mMade = made.mImage;
            }
            else if (row.mKind == TextureKind::File && row.mLaid != nullptr)
            {
                kept.mImage = row.mLaid;
                kept.mFormat = readFormat(*row.mLaid, row.mEncoding);
            }
            else if (row.mKind == TextureKind::File && row.mImage != nullptr)
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
                if (kept.mMade == nullptr || !kept.mMade->getWholeFormat().has_value())
                {
                    levels += kept.mImage.value()->getNumMipmapLevels();
                    texels += laidBytes(*kept.mImage.value(), kept.mFormat);
                }
                if (kept.mMade != nullptr)
                    levels += kept.mMade->getLevels().size();
            }
        mLevels.reserve(levels);
        mTexels.reserve(texels);

        // What the contracts below are taken against: the reserve and the fill agree by argument
        // through branches that push a different number of levels each, and a growth is the failure
        // — in a release build too, where it would hand the backend spans into freed storage.
        const std::size_t reserved = mLevels.capacity();
        const std::size_t reservedTexels = mTexels.capacity();

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

        Crash::contract(mLevels.capacity() == reserved, "the level table grew while descriptions spanned it");
        Crash::contract(mTexels.capacity() == reservedTexels, "the laid texels grew while descriptions spanned them");
    }

    Misc::Result<TextureData, std::string> SceneTextures::describeKept(const Kept& kept)
    {
        if (!kept.mImage.isOk())
            return Misc::Err{ kept.mImage.error() };

        if (const osg::Image* image = kept.mImage.value().get())
        {
            // **A whole chain made in a format of its own** stands as it was made, under its file's name.
            if (kept.mMade != nullptr && kept.mMade->getWholeFormat().has_value())
            {
                const MadeImage& made = *kept.mMade;
                const std::size_t at = mLevels.size();
                mLevels.insert(mLevels.end(), made.getLevels().begin(), made.getLevels().end());
                const MipLevel& top = made.getLevels().front();
                return TextureData{ .mFormat = *made.getWholeFormat(),
                    .mEncoding = kept.mEncoding,
                    .mWidth = top.mWidth,
                    .mHeight = top.mHeight,
                    .mBytes = made.getBytes(),
                    .mLevels = std::span<const MipLevel>(mLevels).subspan(at),
                    .mName = SceneUtil::EmbeddedImage::nameOf(*image) };
            }

            const Misc::Result<TextureData, std::string> read
                = describeImage(*image, kept.mFormat, kept.mEncoding, mLevels, mTexels);
            if (!read.isOk() || kept.mMade == nullptr)
                return read;

            // **The file's finest level and the made levels under it**, in place of the levels the
            // file carried, which the description laid last in the table.
            TextureData described = read.value();
            assert(layoutOf(described.mFormat).mBytes == kept.mMade->getLaid().mBytes
                && "a chain made at another layout than its file is described at");
            assert(layoutOf(described.mFormat).mSide == kept.mMade->getLaid().mSide
                && "a chain made at another layout than its file is described at");
            const auto at = static_cast<std::size_t>(described.mLevels.data() - mLevels.data());
            mLevels.resize(at + 1);
            mLevels.insert(mLevels.end(), kept.mMade->getLevels().begin(), kept.mMade->getLevels().end());
            described.mLevels = std::span<const MipLevel>(mLevels).subspan(at);
            described.mCoarser = kept.mMade->getBytes();
            assert(described.levelsFit() && "a made chain short of its levels");
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
                .mFormat = TextureFormat::Bc7Unorm,
                .mEncoding = kept.mEncoding,
            };

        return TextureData{
            .mSource = TextureSource::GroundComposite,
            .mFrom = chunk.mMaterial,
            .mFormat = TextureFormat::Bc7Srgb,
            .mEncoding = kept.mEncoding,
        };
    }
}
