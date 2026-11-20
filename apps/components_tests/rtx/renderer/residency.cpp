#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // IWYU pragma: keep: the S3TC formats, which Windows's gl.h lacks
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/layers.hpp>
#include <components/rtx/common/holdtier.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/renderer/nulldevice.hpp>
#include <components/rtx/renderer/residency.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;
        constexpr std::uint64_t sChunk = 64 * sMiB;

        /// A BC3 square of `side` with its whole chain, under `name`: a colour file whose own chain stands, at a byte a
        /// texel and sixteen a block, so it is priced as an RGBA8 one of half its side.
        osg::ref_ptr<osg::Image> chainOf(const std::string& name, const std::uint32_t side)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName(name);
            osg::Image::MipmapDataType offsets;
            std::uint64_t bytes = 0;
            for (std::uint32_t level = 0; (side >> level) > 0; ++level)
            {
                if (level > 0)
                    offsets.push_back(static_cast<std::uint32_t>(bytes));
                const std::uint64_t blocks = std::max<std::uint64_t>((side >> level) / 4, 1);
                bytes += blocks * blocks * 16;
            }
            auto* data = new std::uint8_t[bytes]{};
            image->setImage(static_cast<std::int32_t>(side), static_cast<std::int32_t>(side), 1,
                GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_UNSIGNED_BYTE, data,
                osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(offsets);
            return image;
        }

        /// A scene residency plans for over a device with no card, and the holds a test takes on it.
        struct World
        {
            explicit World(const std::uint64_t cap, const std::size_t arenas = 1)
                : mDevice(cap, arenas)
                , mResidency(mDevice, SlotLayout{})
            {
                mResidency.addScene(SceneSlot::world());
            }

            ~World()
            {
                for (TextureHold& hold : mTextures)
                    mScene.drop(std::move(hold));
                for (TextureHold& hold : mExtra)
                    mScene.drop(std::move(hold));
                for (MeshHold& hold : mMeshes)
                    mScene.drop(std::move(hold));
            }

            World(const World&) = delete;
            World& operator=(const World&) = delete;

            /// One frame: what the device and the feed answered, the scene's hand-over, the blocks.
            void frame(const Residency::Shares& shares = Residency::Shares{})
            {
                mResidency.openFrame(mDevice, mFeed);
                std::ignore = mResidency.handOver(handing(shares));
                mResidency.closeFrame(mDevice);
                mFeed.nextFrame();
            }

            Residency::HandOver handing(const Residency::Shares& shares = Residency::Shares{})
            {
                return Residency::HandOver{
                    .mDevice = mDevice, .mSlot = SceneSlot::world(), .mScene = mScene, .mFeed = mFeed, .mShares = shares
                };
            }

            void frames(const std::uint32_t count, const Residency::Shares& shares = Residency::Shares{})
            {
                for (std::uint32_t at = 0; at < count; ++at)
                    frame(shares);
            }

            TextureIndex texture(const std::string& name, const std::uint32_t side, const HoldTier tier)
            {
                osg::ref_ptr<osg::Image> image = chainOf(name, side);
                mImages.push_back(image);
                mTextures.push_back(mScene.takeTexture(tier, VFS::Path::NormalizedView(name.c_str()), *image));
                return mTextures.back().get();
            }

            const TextureResident& residentOf(const TextureIndex texture) const
            {
                return mResidency.getTexture(SceneSlot::world(), texture);
            }

            std::optional<std::uint8_t> standingOf(const TextureIndex texture) const
            {
                if (const auto* stands = std::get_if<TextureStands>(&residentOf(texture).mStanding))
                    return stands->mLevel;
                return std::nullopt;
            }

            NullDevice mDevice;
            NullFeed mFeed;
            Residency mResidency;
            SceneIdentities mIdentities;
            SceneDesc mScene{ mIdentities };
            std::vector<osg::ref_ptr<osg::Image>> mImages;
            std::vector<TextureHold> mTextures;

            /// Holds beside each texture's own, which a test takes and gives back.
            std::vector<TextureHold> mExtra;
            std::vector<MeshHold> mMeshes;
        };

        template <class Op>
        std::size_t countOps(const std::span<const ResidencyOp> ops)
        {
            return static_cast<std::size_t>(
                std::ranges::count_if(ops, [](const ResidencyOp& op) { return std::holds_alternative<Op>(op); }));
        }

        /// **A plan that fits gives every want**: three 512² textures, one near and two shown, under a cap of a
        /// gigabyte. The first hand-over asks the feed for each whole chain and a chunk of the device, and counts the
        /// three standing nothing, of them the near one; the second, both arrived, stands each in a slot at its want,
        /// level 0, uploading its ten levels. Nothing is starved or below its want, and the shown tier is settled.
        TEST(RtxResidencyTest, aPlanThatFitsGivesEveryWant)
        {
            World world(1024 * sMiB);
            const std::vector<TextureIndex> textures{ world.texture("textures/a.dds", 512, HoldTier::Near),
                world.texture("textures/b.dds", 512, HoldTier::Shown),
                world.texture("textures/c.dds", 512, HoldTier::Shown) };

            world.frame();
            ASSERT_EQ(world.mFeed.getAsked().size(), 3u);
            EXPECT_EQ(world.mFeed.getAsked()[0].mLevels, (LevelSpan{ .mFirst = 0, .mEnd = 10 }));
            EXPECT_EQ(world.mDevice.getBlocksHeld(), 1u) << "a chunk asked one ahead";
            EXPECT_EQ(world.mResidency.getReport().mMissingDemanded, (MissingDemanded{ .mAsked = 3 }))
                << "three textures planned, asked of the feed, and standing nothing";
            EXPECT_EQ(world.mResidency.getReport().mMissingNear, (MissingDemanded{ .mAsked = 1 }))
                << "of them the near one";
            EXPECT_FALSE(world.mResidency.isSettled(HoldTier::Shown));
            EXPECT_TRUE(world.mResidency.owes(SceneSlot::world()));

            world.frame();
            const std::span<const ResidencyOp> ops = world.mDevice.getLastOps();
            ASSERT_EQ(countOps<StandImage>(ops), 3u);
            const auto& first = std::get<StandImage>(ops[0]);
            EXPECT_EQ(first.mLevel, 0u);
            EXPECT_EQ(first.mUpload, (LevelSpan{ .mFirst = 0, .mEnd = 10 }));
            EXPECT_FALSE(first.mFrom.has_value());
            for (const TextureIndex texture : textures)
                EXPECT_EQ(world.standingOf(texture), std::optional<std::uint8_t>(0));
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            EXPECT_EQ(world.mResidency.getReport().mBelowWant, 0u);
            EXPECT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
            EXPECT_EQ(world.mResidency.getReport().mMissingDemanded, MissingDemanded{});
            EXPECT_EQ(world.mResidency.getReport().mMissingNear, MissingDemanded{});
            EXPECT_FALSE(world.mResidency.owes(SceneSlot::world()));
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());
        }

        /// **A plan over the cap lowers the least protected first, the most bytes first, to floors and no further**:
        /// under a cap of one chunk, a shown 2048² texture and a hidden one stand in a medium slab, a chunk, and a
        /// hidden 512² in a small slab, another. The hidden 2048² is lowered first, as the most bytes of the lowest
        /// tier, down to its floor, level 5, 64 texels a side; then the hidden 512² to its floor, level 3; and only
        /// then the shown one, two levels, since its 1024² chain of 1.4 MiB is medium still, which takes it out of the
        /// medium chunk and all three into one chunk of small slabs.
        TEST(RtxResidencyTest, aPlanOverTheCapLowersTheLeastProtectedMostBytesFirstToFloors)
        {
            World world(sChunk);
            const TextureIndex shown = world.texture("textures/shown.dds", 2048, HoldTier::Shown);
            const TextureIndex hidden = world.texture("textures/hidden.dds", 2048, HoldTier::Hidden);
            const TextureIndex small = world.texture("textures/small.dds", 512, HoldTier::Hidden);
            world.frames(3);

            EXPECT_EQ(world.residentOf(shown).mPlannedLevel, std::optional<std::uint8_t>(2));
            EXPECT_EQ(world.residentOf(hidden).mPlannedLevel, std::optional<std::uint8_t>(5));
            EXPECT_EQ(world.residentOf(hidden).mFloorLevel, 5u);
            EXPECT_EQ(world.residentOf(small).mPlannedLevel, std::optional<std::uint8_t>(3));
            EXPECT_EQ(world.mResidency.getReport().mBelowWant, 3u);
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            EXPECT_LE(world.mResidency.getReport().mPlannedBytes, sChunk);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(2));
            EXPECT_EQ(world.standingOf(hidden), std::optional<std::uint8_t>(5));
            EXPECT_EQ(world.standingOf(small), std::optional<std::uint8_t>(3));
        }

        /// **What nothing demands is kept while it fits nine tenths of what the demand leaves, the most lately held
        /// first, else a level coarser, else dropped**: textures stood shown under a large cap, then let go of to kept
        /// under a cap of ten chunks with one shown texture's chunk demanded. Each kept 4096² stands in a block of its
        /// own; as many stay as fit the share, and the rest are dropped, their slots given back.
        TEST(RtxResidencyTest, theCacheKeepsNineTenthsOfWhatTheDemandLeaves)
        {
            World world(4096 * sMiB);
            const TextureIndex shown = world.texture("textures/shown.dds", 512, HoldTier::Shown);
            std::vector<TextureIndex> kept;
            kept.reserve(30);
            for (std::uint32_t at = 0; at < 30; ++at)
                kept.push_back(world.texture("textures/kept" + std::to_string(at) + ".dds", 4096, HoldTier::Kept));
            // Shown first, so they stand: what nothing demands is kept only where it stands. Under a share that takes
            // them all in a frame.
            for (const TextureIndex texture : kept)
                world.mExtra.push_back(world.mScene.holdTexture(texture, HoldTier::Shown));
            const Residency::Shares wide{ .mUploadBytes = 4096 * sMiB };
            world.frames(4, wide);
            for (const TextureIndex texture : kept)
                ASSERT_EQ(world.standingOf(texture), std::optional<std::uint8_t>(0));

            for (TextureHold& hold : world.mExtra)
                world.mScene.drop(std::move(hold));
            world.mExtra.clear();
            constexpr std::uint64_t sCap = 10 * sChunk;
            world.mDevice.setCap(sCap);
            world.frames(2, wide);

            const std::uint64_t demanded = world.mResidency.getReport().mDemandedBytes;
            EXPECT_EQ(demanded, sChunk) << "the shown texture's chunk";
            const std::uint64_t limit = demanded + (sCap - demanded) * 9 / 10;
            const std::uint64_t stride
                = world.mResidency.getLedger().getStrideBytes(world.residentOf(kept[0]).mClassAt[0]);
            const auto fitting = static_cast<std::size_t>((limit - demanded) / stride);
            const auto standing = static_cast<std::size_t>(std::ranges::count_if(
                kept, [&](const TextureIndex texture) { return world.standingOf(texture).has_value(); }));
            EXPECT_EQ(standing, fitting);
            EXPECT_LE(world.mResidency.getReport().mPlannedBytes, limit);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(0));
        }

        /// **A want that falls is held `sWantHoldHandOvers` before the plan takes it, and one that rises is taken at
        /// once**: a 2048² read at a footprint step of 21 asks level 0 (21 − 32 + log2 2048 = 0); asked at 24 instead
        /// it asks level 3, which the plan gives only on the thirtieth hand-over after; asked at 21 again, level 0 at
        /// once.
        TEST(RtxResidencyTest, aWantThatFallsIsHeldAndOneThatRisesIsTakenAtOnce)
        {
            World world(1024 * sMiB);
            const TextureIndex texture = world.texture("textures/wall.dds", 2048, HoldTier::Shown);
            const MaterialIndex material = world.mScene.addMaterial(Material{ .mDiffuse = texture });
            MaterialHold held = world.mScene.holdMaterial(material, HoldTier::Shown);
            world.mScene.askMaterial(material, 21);
            world.frames(3);
            ASSERT_EQ(world.residentOf(texture).mWantedLevel, 0u);

            world.mScene.unaskMaterial(material, 21);
            world.mScene.askMaterial(material, 24);
            world.frames(Residency::sWantHoldHandOvers);
            EXPECT_EQ(world.residentOf(texture).mWantedLevel, 0u) << "a fallen want taken before it stood";
            world.frame();
            EXPECT_EQ(world.residentOf(texture).mWantedLevel, 3u);
            EXPECT_EQ(world.residentOf(texture).mPlannedLevel, std::optional<std::uint8_t>(3));

            world.mScene.unaskMaterial(material, 24);
            world.mScene.askMaterial(material, 21);
            world.frame();
            EXPECT_EQ(world.residentOf(texture).mWantedLevel, 0u);
            world.mScene.unaskMaterial(material, 21);
            world.mScene.drop(std::move(held));
        }

        /// **A texture is raised only where the plan fits a chunk under the cap, however long it waited, and what
        /// stood stays until the raise rides**: stood two levels down, out of a medium chunk, under a cap of one, it
        /// stays there for a hundred hand-overs under a cap of two, which would hold it at its want with nothing to
        /// spare. Under three, the raise asks the two levels it lacks while the old slot stands on; the arrival stands
        /// the chain from that slot, uploading levels 0 and 1, and gives the old slot back only then.
        TEST(RtxResidencyTest, aRaiseWaitsForAChunkUnderTheCapAndKeepsWhatStoodUntilItRides)
        {
            World world(sChunk);
            const TextureIndex shown = world.texture("textures/shown.dds", 2048, HoldTier::Shown);
            const TextureIndex other = world.texture("textures/other.dds", 512, HoldTier::Shown);
            world.frames(3);
            ASSERT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(2));
            const Slot stood = std::get<TextureStands>(world.residentOf(shown).mStanding).mSlot;

            world.mDevice.setCap(2 * sChunk);
            world.frames(100);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(2)) << "raised with no chunk to spare";

            world.mDevice.setCap(3 * sChunk);
            world.frame();
            const auto* arriving = std::get_if<TextureArriving>(&world.residentOf(shown).mStanding);
            ASSERT_NE(arriving, nullptr);
            EXPECT_EQ(arriving->mToLevel, 0u);
            EXPECT_EQ(world.mResidency.getReport().mMissingDemanded, MissingDemanded{})
                << "a raise on its way stands what is below it";
            EXPECT_GT(world.mResidency.getReport().mWaitingDemanded, 0u);
            ASSERT_TRUE(arriving->mNow.has_value());
            EXPECT_EQ(arriving->mNow->mSlot, stood);
            EXPECT_EQ(arriving->mNow->mLevel, 2u);
            EXPECT_EQ(world.mFeed.getAsked().back().mLevels, (LevelSpan{ .mFirst = 0, .mEnd = 2 }));
            EXPECT_EQ(countOps<DropImage>(world.mDevice.getLastOps()), 0u) << "what stood dropped before the raise";

            // **The raise is one level step in view**, counted on the hand-over it stood on: `other` stood at its want
            // all along.
            std::uint32_t steps = world.mResidency.getReport().mLevelSteps;
            for (std::uint32_t frame = 0; frame < 3; ++frame)
            {
                world.frame();
                steps += world.mResidency.getReport().mLevelSteps;
            }
            EXPECT_EQ(steps, 1u);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(0));
            EXPECT_EQ(world.standingOf(other), std::optional<std::uint8_t>(0));
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());
        }

        /// **At a full arena a bake stands once a lowering's slot retires, the bake planned as one with its source**: a
        /// shown BC3 2048² stands in the cap's one chunk, of medium slabs. Its sprite's light bake, RGBA8 at the
        /// source's level, and a BC3 512² are taken beside it. At level 2 the source's 512² chain is small (256 KiB ×
        /// 4/3 under the small stride's 512 KiB) but the bake's is medium (1 MiB × 4/3), two chunks; at level 3 both
        /// are small, one chunk with the 512²'s at its want. The arena holds that chunk's worth already, of the wrong
        /// slabs, so the lowering stands in the transit chunk; the medium chunk goes back once its slot retires, and
        /// the bake and the 512² stand: nothing is refused for good, and the cap is passed by the transit chunk alone.
        TEST(RtxResidencyTest, aBakeAtAFullArenaStandsOnceALoweringsSlotRetires)
        {
            World world(sChunk);
            const TextureIndex file = world.texture("textures/tx_fire.dds", 2048, HoldTier::Shown);
            world.frames(3);
            ASSERT_EQ(world.standingOf(file), std::optional<std::uint8_t>(0));

            const TextureIndex bake
                = world.mScene.textures().addSpriteLight(VFS::Path::NormalizedView("textures/tx_fire.dds"));
            ASSERT_NE(bake, sNoRow);
            world.mExtra.push_back(world.mScene.holdTexture(bake, HoldTier::Shown));
            const TextureIndex other = world.texture("textures/other.dds", 512, HoldTier::Shown);
            for (std::uint32_t at = 0; at < 16; ++at)
            {
                world.frame();
                EXPECT_LE(world.mDevice.getBlocksHeld(), 2u) << "past the transit chunk at frame " << at;
            }

            EXPECT_EQ(world.standingOf(file), std::optional<std::uint8_t>(3));
            EXPECT_EQ(world.standingOf(bake), std::optional<std::uint8_t>(3));
            EXPECT_EQ(world.standingOf(other), std::optional<std::uint8_t>(0));
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            EXPECT_EQ(world.mDevice.getBlocksHeld(), 1u);
            EXPECT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());
        }

        /// **A composite stands once its layers stand, and holds them at its own tier while it waits**: a chunk of two
        /// layers, held hidden, and its composite held shown. While it waits its layers are shown, asked of the feed,
        /// the composite not yet asked; the hand-over their texels land on stands them first and the composite after
        /// them, reading both layers' slots; made, its layers go back to hidden.
        TEST(RtxResidencyTest, aCompositeStandsOnceItsLayersStand)
        {
            World world(1024 * sMiB);
            const TextureIndex under = world.texture("textures/under.dds", 256, HoldTier::Hidden);
            const TextureIndex over = world.texture("textures/over.dds", 256, HoldTier::Hidden);
            constexpr std::array<float, 4> sWeights{ 1.0f, 0.0f, 0.0f, 1.0f };
            const std::array layers{ Testing::layerOf(under),
                Testing::layerOf(over, world.mScene.materials().addMask(sWeights), 2, 2) };
            Material chunk;
            chunk.mKind = MaterialKind::Terrain;
            chunk.mFlatten = GroundFlattening::Every;
            chunk.mLayers = world.mScene.materials().addLayers(layers);
            const MaterialIndex material = world.mScene.addMaterial(chunk);
            const TextureIndex ground = world.mScene.textures().addGround(material, TextureKind::GroundAlbedo);
            ASSERT_NE(ground, sNoRow);
            world.mExtra.push_back(world.mScene.holdTexture(ground, HoldTier::Shown));

            world.frame();
            EXPECT_EQ(countOps<StandImage>(world.mDevice.getLastOps()), 0u) << "the layers' texels asked";
            EXPECT_EQ(world.residentOf(under).mTier, HoldTier::Shown) << "a waiting composite's layer at its tier";
            EXPECT_EQ(world.residentOf(over).mTier, HoldTier::Shown);
            EXPECT_EQ(world.mResidency.getReport().mMissingDemanded,
                (MissingDemanded{ .mAsked = 2, .mUnaskedComposites = 1 }))
                << "the layers on their way, the composite not yet asked";

            world.frame();
            EXPECT_EQ(world.standingOf(under), std::optional<std::uint8_t>(0));
            EXPECT_EQ(world.standingOf(over), std::optional<std::uint8_t>(0));
            ASSERT_EQ(world.standingOf(ground), std::optional<std::uint8_t>(0)) << "made in the layers' hand-over";
            const std::span<const ResidencyOp> ops = world.mDevice.getLastOps();
            ASSERT_EQ(countOps<StandImage>(ops), 3u);
            const auto& baked = std::get<StandImage>(ops[2]);
            EXPECT_EQ(baked.mTexture, ground) << "after the layers it is made of";
            ASSERT_NE(baked.mTexels, nullptr);
            EXPECT_EQ(baked.mTexels->mSource, TextureSource::GroundComposite);
            EXPECT_EQ(std::vector<TextureIndex>(baked.mTexels->mReads.begin(), baked.mTexels->mReads.end()),
                (std::vector<TextureIndex>{ under, over }));

            world.frame();
            EXPECT_EQ(world.residentOf(under).mTier, HoldTier::Hidden) << "a made composite's layer back at its own";
        }

        /// **What fits nowhere is starved and named**: under a cap of nought every shown texture's floor is starved,
        /// the report counts both, and nothing stands.
        TEST(RtxResidencyTest, aFloorThatFitsNowhereIsStarvedAndNamed)
        {
            World world(0);
            const TextureIndex one = world.texture("textures/one.dds", 512, HoldTier::Shown);
            const TextureIndex two = world.texture("textures/two.dds", 128, HoldTier::Shown);
            world.frames(2);
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 2u);
            EXPECT_EQ(world.residentOf(one).mPlannedLevel, std::nullopt);
            EXPECT_EQ(world.standingOf(two), std::nullopt);
            EXPECT_EQ(world.mDevice.getBlocksHeld(), 0u);
        }

        /// **A completion for a tenant that left is dropped**: a texture's texels asked, its slot given back and taken
        /// by another file before they arrive. The arrival names the old generation and stands nothing; the new tenant
        /// asks for its own and stands from them.
        TEST(RtxResidencyTest, aCompletionForAnotherTenantIsDropped)
        {
            World world(1024 * sMiB);
            const TextureIndex first = world.texture("textures/first.dds", 512, HoldTier::Shown);
            world.frame();
            ASSERT_EQ(world.mFeed.getAsked().size(), 1u);
            const std::uint32_t generation = world.residentOf(first).mGeneration;

            world.mScene.drop(std::move(world.mTextures.back()));
            world.mTextures.pop_back();
            const TextureIndex second = world.texture("textures/second.dds", 256, HoldTier::Shown);
            ASSERT_EQ(second, first) << "the slot taken again";
            world.frame();
            EXPECT_EQ(world.residentOf(second).mGeneration, generation + 1);
            EXPECT_EQ(countOps<StandImage>(world.mDevice.getLastOps()), 0u) << "the old tenant's texels stood";
            ASSERT_EQ(world.mFeed.getAsked().size(), 2u);
            EXPECT_EQ(world.mFeed.getAsked()[1].mGeneration, generation + 1);

            world.frame();
            EXPECT_EQ(world.standingOf(second), std::optional<std::uint8_t>(0));
            const auto& stood = std::get<StandImage>(world.mDevice.getLastOps()[0]);
            EXPECT_EQ(stood.mUpload, (LevelSpan{ .mFirst = 0, .mEnd = 9 })) << "the 256² tenant's nine levels";
        }

        /// **A structure is built loose, then copied tight once the device answers its size**; one that refits is kept
        /// as built. A near quad's mesh: its build on the second frame, the chunk asked on the first, where it is a
        /// near mesh missing; the device's answer the frame after, and its tight copy then, the loose slot given back.
        TEST(RtxResidencyTest, aStructureIsBuiltLooseThenCopiedTight)
        {
            World world(1024 * sMiB);
            const MeshIndex mesh = Testing::addQuadMesh(world.mScene);
            world.mMeshes.push_back(world.mScene.holdMesh(mesh, HoldTier::Near));
            world.frame();
            EXPECT_EQ(
                world.mResidency.getReport().mMissingDemanded, (MissingDemanded{ .mMeshes = 1, .mMeshesForRoom = 1 }))
                << "no room for it until the chunk asked comes";
            EXPECT_EQ(world.mResidency.getReport().mMissingNear, (MissingDemanded{ .mMeshes = 1 }))
                << "the room's count the whole's alone";
            world.frame();
            EXPECT_EQ(world.mResidency.getReport().mMissingNear, MissingDemanded{}) << "built on its second frame";
            ASSERT_EQ(countOps<BuildStructure>(world.mDevice.getLastOps()), 1u);
            const auto& build = std::get<BuildStructure>(world.mDevice.getLastOps()[0]);
            EXPECT_EQ(build.mMesh, mesh);
            EXPECT_EQ(build.mBytes, 512u) << "two triangles at 64 bytes and a unit, rounded";
            EXPECT_TRUE(std::holds_alternative<StructureLoose>(
                world.mResidency.getStructure(SceneSlot::world(), mesh).mStanding));

            world.frame();
            const StructureResident& structure = world.mResidency.getStructure(SceneSlot::world(), mesh);
            EXPECT_TRUE(std::holds_alternative<StructureTight>(structure.mStanding));
            EXPECT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
        }

        /// **A drain moves each of its slots once**: a chunk filled with 512² chains, all but two of each slab's
        /// dropped, and 256² chains taken, whose class has fewer free slabs than they need: the 512²s' slabs drain,
        /// each moving its two in one hand-over into another's free slots, each slot once, and every 256² stands.
        TEST(RtxResidencyTest, aDrainMovesEachOfItsSlotsOnce)
        {
            World world(sChunk);
            for (std::uint32_t at = 0; at < 176; ++at)
                world.texture("textures/fill" + std::to_string(at) + ".dds", 512, HoldTier::Shown);
            world.frames(12);
            const SlotLedger& ledger = world.mResidency.getLedger();

            std::vector<std::uint32_t> keptIn;
            for (std::size_t at = world.mTextures.size(); at-- > 0;)
            {
                const auto* stands = std::get_if<TextureStands>(&world.residentOf(world.mTextures[at].get()).mStanding);
                ASSERT_NE(stands, nullptr);
                const auto slab = static_cast<std::size_t>(stands->mSlot.mSlab.at());
                keptIn.resize(std::max(keptIn.size(), slab + 1), 0);
                if (keptIn[slab] < 2)
                {
                    ++keptIn[slab];
                    continue;
                }
                world.mScene.drop(std::move(world.mTextures[at]));
                world.mTextures.erase(world.mTextures.begin() + static_cast<std::ptrdiff_t>(at));
            }
            world.frames(4);
            ASSERT_EQ(world.mTextures.size(), 32u) << "two in each of the chunk's sixteen slabs";
            ASSERT_EQ(ledger.count().mSlabsFree, 0u);

            for (std::uint32_t at = 0; at < 300; ++at)
                world.texture("textures/small" + std::to_string(at) + ".dds", 256, HoldTier::Shown);
            std::uint32_t frames = 0;
            std::uint32_t moved = 0;
            std::uint32_t mostInOne = 0;
            do
            {
                ASSERT_LT(++frames, 200u) << "what is planned never stood";
                world.frame();
                std::uint32_t moves = 0;
                for (const ResidencyOp& op : world.mDevice.getLastOps())
                    if (const auto* stand = std::get_if<StandImage>(&op);
                        stand != nullptr && stand->mFrom.has_value() && stand->mUpload.empty())
                        ++moves;
                moved += moves;
                mostInOne = std::max(mostInOne, moves);
                ASSERT_TRUE(ledger.isConsistent()) << "frame " << frames;
            } while (!world.mResidency.isSettled(HoldTier::Shown));
            // Nine slabs drained, two slots each, and a drain's two moved in one hand-over: the second move was once
            // issued again from the slot the first had left, a slot the device was handed back twice.
            EXPECT_EQ(moved, 18u);
            EXPECT_EQ(mostInOne, 2u);
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            for (std::size_t at = 32; at < world.mTextures.size(); ++at)
                EXPECT_EQ(world.standingOf(world.mTextures[at].get()), std::optional<std::uint8_t>(0));
        }

        /// **A drain never waits on another scene's hand-over**: a view's scene fills a chunk's sixteen slabs with 512²
        /// chains and gives back all but two of each slab's; the world then takes 256² chains, whose class has no slab.
        /// No drain picks a slab of the view's, which the world's hand-over cannot move and the view, never handed over
        /// again, never would: the world's chains stand in a chunk past the plan's, where once they waited for good.
        TEST(RtxResidencyTest, aDrainNeverWaitsOnAnotherScenesHandOver)
        {
            World world(2 * sChunk);
            const SceneSlot viewSlot = SceneSlot::view(0);
            world.mResidency.addScene(viewSlot);
            SceneIdentities viewIdentities;
            SceneDesc view{ viewIdentities };
            std::vector<osg::ref_ptr<osg::Image>> images;
            std::vector<TextureHold> holds;
            const auto handView = [&] {
                world.mResidency.openFrame(world.mDevice, world.mFeed);
                std::ignore = world.mResidency.handOver(Residency::HandOver{
                    .mDevice = world.mDevice, .mSlot = viewSlot, .mScene = view, .mFeed = world.mFeed });
                world.mResidency.closeFrame(world.mDevice);
                world.mFeed.nextFrame();
            };
            for (std::uint32_t at = 0; at < 176; ++at)
            {
                const std::string name = "textures/view" + std::to_string(at) + ".dds";
                images.push_back(chainOf(name, 512));
                holds.push_back(
                    view.takeTexture(HoldTier::Shown, VFS::Path::NormalizedView(name.c_str()), *images.back()));
            }
            for (std::uint32_t frame = 0; frame < 16; ++frame)
                handView();
            std::vector<std::uint32_t> keptIn;
            for (std::size_t at = holds.size(); at-- > 0;)
            {
                const auto* stands
                    = std::get_if<TextureStands>(&world.mResidency.getTexture(viewSlot, holds[at].get()).mStanding);
                if (stands == nullptr)
                    continue;
                const auto slab = static_cast<std::size_t>(stands->mSlot.mSlab.at());
                keptIn.resize(std::max(keptIn.size(), slab + 1), 0);
                if (keptIn[slab] < 2)
                {
                    ++keptIn[slab];
                    continue;
                }
                view.drop(std::move(holds[at]));
                holds.erase(holds.begin() + static_cast<std::ptrdiff_t>(at));
            }
            for (std::uint32_t frame = 0; frame < 4; ++frame)
                handView();
            world.mDevice.setCap(sChunk + sChunk / 2);

            for (std::uint32_t at = 0; at < 300; ++at)
                world.texture("textures/small" + std::to_string(at) + ".dds", 256, HoldTier::Shown);
            std::uint32_t frames = 0;
            do
            {
                ASSERT_LT(++frames, 200u) << "the world's chains waited on the view's drain";
                world.frame();
                EXPECT_FALSE(world.mResidency.getReport().mDrainWaitsOnScene)
                    << "a drain of the view's slots, frame " << frames;
            } while (!world.mResidency.isSettled(HoldTier::Shown));
            for (const TextureHold& small : world.mTextures)
                EXPECT_TRUE(world.standingOf(small.get()).has_value());
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());

            for (TextureHold& hold : holds)
                view.drop(std::move(hold));
            world.mResidency.forgetScene(viewSlot, world.mFeed);
        }

        /// **Texels that fail make the texture failed, and what stood of it is given back**: stood two levels down
        /// beside a 512² under a cap of a chunk, its raise under three asks the feed, which fails it. The texture is
        /// failed, planned at nothing, and the next hand-over drops its old slot; it is asked of the feed no more.
        TEST(RtxResidencyTest, texelsThatFailGiveBackWhatStood)
        {
            World world(sChunk);
            const TextureIndex texture = world.texture("textures/broken.dds", 2048, HoldTier::Shown);
            world.texture("textures/other.dds", 512, HoldTier::Shown);
            world.frames(3);
            ASSERT_EQ(world.standingOf(texture), std::optional<std::uint8_t>(2));
            const Slot stood = std::get<TextureStands>(world.residentOf(texture).mStanding).mSlot;

            world.mFeed.fail(texture);
            world.mDevice.setCap(3 * sChunk);
            world.frame();
            const std::size_t asked = world.mFeed.getAsked().size();
            EXPECT_EQ(world.mFeed.getAsked().back().mLevels, (LevelSpan{ .mFirst = 0, .mEnd = 2 }))
                << "the raise asks only the levels it lacks";
            world.frame();
            EXPECT_TRUE(std::holds_alternative<StandingFailed>(world.residentOf(texture).mStanding));
            EXPECT_EQ(world.residentOf(texture).mPlannedLevel, std::nullopt);
            ASSERT_EQ(world.mDevice.getLastOps().size(), 1u);
            EXPECT_EQ(world.mDevice.getLastOps()[0], ResidencyOp(DropImage{ .mTexture = texture, .mFrom = stood }));
            world.frames(3);
            EXPECT_EQ(world.mFeed.getAsked().size(), asked);
            EXPECT_FALSE(world.mResidency.owes(SceneSlot::world()));
        }

        /// **A forgotten scene's slots retire with the device's scene, at the last epoch handed out**, and a scene that
        /// takes its place stands its content again in slots the device saw retire: two textures and a structure stand;
        /// the scene is forgotten as the device lets its slot go, and the scene added again stands all three anew with
        /// no slot stood twice or before its epoch.
        TEST(RtxResidencyTest, aForgottenSceneGivesBackItsSlots)
        {
            World world(1024 * sMiB);
            world.texture("textures/a.dds", 512, HoldTier::Shown);
            world.texture("textures/b.dds", 128, HoldTier::Shown);
            world.mMeshes.push_back(world.mScene.holdMesh(Testing::addQuadMesh(world.mScene), HoldTier::Shown));
            world.frames(3);
            ASSERT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
            // Two textures and the structure's tight copy, its loose slot retiring behind the copy.
            const SlotCounts stood = world.mResidency.getLedger().count();
            EXPECT_EQ(stood.mSlotsLive, 3u);
            EXPECT_EQ(stood.mSlotsRetiring, 1u);

            world.mResidency.forgetScene(SceneSlot::world(), world.mFeed);
            world.mDevice.forgetScene(SceneSlot::world());
            EXPECT_FALSE(world.mResidency.holds(SceneSlot::world()));
            EXPECT_EQ(world.mResidency.getLedger().count().mSlotsLive, 0u);
            EXPECT_EQ(world.mResidency.getLedger().count().mSlotsRetiring, 4u);

            world.mResidency.addScene(SceneSlot::world());
            world.frames(4);
            EXPECT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
            EXPECT_EQ(world.mResidency.getLedger().count().mSlotsLive, 3u);
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());
        }

        /// **Residency's contract holds over random plans**: two scenes' textures and meshes taken, held at random
        /// tiers, asked and let go of, under a cap that moves, for four hundred frames. The device asserts no slot
        /// stood twice or before its epoch, the ledger stays consistent, the frame's shares are never passed, and once
        /// the content stops moving under a cap that holds it all, everything planned stands within a bounded number
        /// of frames.
        TEST(RtxResidencyTest, theContractHoldsOverRandomPlans)
        {
            World world(8 * sChunk);
            // NOLINTNEXTLINE(bugprone-random-generator-seed): one sequence, so a failure is the same failure each run.
            std::mt19937 random(20261120);
            std::vector<TextureHold>& extra = world.mExtra;
            const Residency::Shares shares{
                .mUploadBytes = 4 * sMiB, .mCopyBytes = 2 * sMiB, .mBuildTriangles = 64, .mTightCopies = 4
            };
            for (std::uint32_t frame = 0; frame < 400; ++frame)
            {
                switch (random() % 6)
                {
                    case 0:
                    {
                        const auto side = std::uint32_t{ 64 } << (random() % 7);
                        const auto tier = static_cast<HoldTier>(random() % 4);
                        world.texture("textures/t" + std::to_string(frame) + ".dds", side, tier);
                        break;
                    }
                    case 1:
                        if (!world.mTextures.empty())
                        {
                            const std::size_t at = random() % world.mTextures.size();
                            world.mScene.drop(std::move(world.mTextures[at]));
                            world.mTextures.erase(world.mTextures.begin() + static_cast<std::ptrdiff_t>(at));
                        }
                        break;
                    case 2:
                        if (!world.mTextures.empty())
                            extra.push_back(world.mScene.holdTexture(
                                world.mTextures[random() % world.mTextures.size()].get(), HoldTier::Shown));
                        break;
                    case 3:
                        if (!extra.empty())
                        {
                            world.mScene.drop(std::move(extra.back()));
                            extra.pop_back();
                        }
                        break;
                    case 4:
                        world.mMeshes.push_back(world.mScene.holdMesh(
                            Testing::addQuadMesh(world.mScene), static_cast<HoldTier>(random() % 3)));
                        break;
                    default:
                        world.mDevice.setCap((2 + random() % 12) * sChunk);
                        break;
                }
                world.frame(shares);
                const ResidencyReport& report = world.mResidency.getReport();
                ASSERT_TRUE(world.mResidency.getLedger().isConsistent()) << "frame " << frame;
                ASSERT_LE(report.mTightCopies, shares.mTightCopies) << "frame " << frame;
                // An upload is issued while any of the share is left, so all but the frame's last stand under it.
                std::uint64_t lastUpload = 0;
                for (const ResidencyOp& op : world.mDevice.getLastOps())
                    if (const auto* stand = std::get_if<StandImage>(&op); stand != nullptr && !stand->mUpload.empty())
                        lastUpload = world.mResidency.getLedger().getStrideBytes(
                            world.mResidency.getLedger().getClassOf(stand->mTo.mSlot));
                if (report.mBytesUploaded > 0)
                    ASSERT_LT(report.mBytesUploaded - lastUpload, shares.mUploadBytes) << "frame " << frame;
            }

            for (TextureHold& hold : extra)
                world.mScene.drop(std::move(hold));
            extra.clear();
            world.mDevice.setCap(64 * sChunk);
            std::uint32_t frames = 0;
            while (!world.mResidency.isSettled(HoldTier::Hidden))
            {
                ASSERT_LT(++frames, 200u) << "what is planned never stood";
                world.frame(shares);
            }
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());
        }
    }
}
