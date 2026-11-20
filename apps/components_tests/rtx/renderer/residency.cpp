#include <algorithm>
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
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/geometry.hpp>
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

        /// An RGBA8 square of `side` with its whole chain, under `name`: what a file's header says of it.
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
                bytes += std::uint64_t{ side >> level } * (side >> level) * 4;
            }
            auto* data = new std::uint8_t[bytes]{};
            image->setImage(static_cast<std::int32_t>(side), static_cast<std::int32_t>(side), 1, GL_RGBA8, GL_RGBA,
                GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
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

        /// **A plan that fits gives every want**: three 256² textures, shown, under a cap of a gigabyte. The first
        /// hand-over asks the feed for each whole chain and a chunk of the device; the second, both arrived, stands
        /// each in a slot at its want, level 0, uploading its nine levels. Nothing is starved or below its want, and
        /// the shown tier is settled.
        TEST(RtxResidencyTest, aPlanThatFitsGivesEveryWant)
        {
            World world(1024 * sMiB);
            const std::vector<TextureIndex> textures{ world.texture("textures/a.dds", 256, HoldTier::Shown),
                world.texture("textures/b.dds", 256, HoldTier::Shown),
                world.texture("textures/c.dds", 256, HoldTier::Shown) };

            world.frame();
            ASSERT_EQ(world.mFeed.getAsked().size(), 3u);
            EXPECT_EQ(world.mFeed.getAsked()[0].mLevels, (LevelSpan{ .mFirst = 0, .mEnd = 9 }));
            EXPECT_EQ(world.mDevice.getBlocksHeld(), 1u) << "a chunk asked one ahead";
            EXPECT_FALSE(world.mResidency.isSettled(HoldTier::Shown));
            EXPECT_TRUE(world.mResidency.owes(SceneSlot::world()));

            world.frame();
            const std::span<const ResidencyOp> ops = world.mDevice.getLastOps();
            ASSERT_EQ(countOps<StandImage>(ops), 3u);
            const auto& first = std::get<StandImage>(ops[0]);
            EXPECT_EQ(first.mLevel, 0u);
            EXPECT_EQ(first.mUpload, (LevelSpan{ .mFirst = 0, .mEnd = 9 }));
            EXPECT_FALSE(first.mFrom.has_value());
            for (const TextureIndex texture : textures)
                EXPECT_EQ(world.standingOf(texture), std::optional<std::uint8_t>(0));
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            EXPECT_EQ(world.mResidency.getReport().mBelowWant, 0u);
            EXPECT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
            EXPECT_FALSE(world.mResidency.owes(SceneSlot::world()));
            EXPECT_TRUE(world.mResidency.getLedger().isConsistent());
        }

        /// **A plan over the cap lowers the least protected first, the most bytes first, to floors and no further**:
        /// under a cap of one chunk, a shown 1024² texture and a hidden one stand in a medium slab, a chunk, and a
        /// hidden 256² in a small slab, another. The hidden 1024² is lowered first, as the most bytes of the lowest
        /// tier, down to its floor, level 4, 64 texels a side; then the hidden 256² to its floor, level 2; and only
        /// then the shown one, two levels, since its 512² chain of 1.4 MiB is medium still, which takes it out of the
        /// medium chunk and all three into one chunk of small slabs.
        TEST(RtxResidencyTest, aPlanOverTheCapLowersTheLeastProtectedMostBytesFirstToFloors)
        {
            World world(sChunk);
            const TextureIndex shown = world.texture("textures/shown.dds", 1024, HoldTier::Shown);
            const TextureIndex hidden = world.texture("textures/hidden.dds", 1024, HoldTier::Hidden);
            const TextureIndex small = world.texture("textures/small.dds", 256, HoldTier::Hidden);
            world.frames(3);

            EXPECT_EQ(world.residentOf(shown).mPlannedLevel, std::optional<std::uint8_t>(2));
            EXPECT_EQ(world.residentOf(hidden).mPlannedLevel, std::optional<std::uint8_t>(4));
            EXPECT_EQ(world.residentOf(hidden).mFloorLevel, 4u);
            EXPECT_EQ(world.residentOf(small).mPlannedLevel, std::optional<std::uint8_t>(2));
            EXPECT_EQ(world.mResidency.getReport().mBelowWant, 3u);
            EXPECT_EQ(world.mResidency.getReport().mStarvedTextures, 0u);
            EXPECT_LE(world.mResidency.getReport().mPlannedBytes, sChunk);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(2));
            EXPECT_EQ(world.standingOf(hidden), std::optional<std::uint8_t>(4));
            EXPECT_EQ(world.standingOf(small), std::optional<std::uint8_t>(2));
        }

        /// **What nothing demands is kept while it fits nine tenths of what the demand leaves, the most lately held
        /// first, else a level coarser, else dropped**: textures stood shown under a large cap, then let go of to kept
        /// under a cap of ten chunks with one shown texture's chunk demanded. Each kept 2048² stands in a block of its
        /// own; as many stay as fit the share, and the rest are dropped, their slots given back.
        TEST(RtxResidencyTest, theCacheKeepsNineTenthsOfWhatTheDemandLeaves)
        {
            World world(4096 * sMiB);
            const TextureIndex shown = world.texture("textures/shown.dds", 256, HoldTier::Shown);
            std::vector<TextureIndex> kept;
            kept.reserve(30);
            for (std::uint32_t at = 0; at < 30; ++at)
                kept.push_back(world.texture("textures/kept" + std::to_string(at) + ".dds", 2048, HoldTier::Kept));
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
        /// once**: a 1024² read at a footprint step of 22 asks level 0; asked at 25 instead it asks level 3, which the
        /// plan gives only on the thirtieth hand-over after; asked at 22 again, level 0 at once.
        TEST(RtxResidencyTest, aWantThatFallsIsHeldAndOneThatRisesIsTakenAtOnce)
        {
            World world(1024 * sMiB);
            const TextureIndex texture = world.texture("textures/wall.dds", 1024, HoldTier::Shown);
            const MaterialIndex material = world.mScene.addMaterial(Material{ .mDiffuse = texture });
            MaterialHold held = world.mScene.holdMaterial(material, HoldTier::Shown);
            world.mScene.askMaterial(material, 22);
            world.frames(3);
            ASSERT_EQ(world.residentOf(texture).mWantedLevel, 0u);

            world.mScene.unaskMaterial(material, 22);
            world.mScene.askMaterial(material, 25);
            world.frames(Residency::sWantHoldHandOvers);
            EXPECT_EQ(world.residentOf(texture).mWantedLevel, 0u) << "a fallen want taken before it stood";
            world.frame();
            EXPECT_EQ(world.residentOf(texture).mWantedLevel, 3u);
            EXPECT_EQ(world.residentOf(texture).mPlannedLevel, std::optional<std::uint8_t>(3));

            world.mScene.unaskMaterial(material, 25);
            world.mScene.askMaterial(material, 22);
            world.frame();
            EXPECT_EQ(world.residentOf(texture).mWantedLevel, 0u);
            world.mScene.unaskMaterial(material, 22);
            world.mScene.drop(std::move(held));
        }

        /// **A texture is raised only where the plan fits a chunk under the cap**: stood two levels down, out of a
        /// medium chunk, under a cap of one, it stays there under a cap of two, which would hold it at its want with
        /// nothing to spare, and is raised once a chunk spares.
        TEST(RtxResidencyTest, aRaiseWaitsForAChunkUnderTheCap)
        {
            World world(sChunk);
            const TextureIndex shown = world.texture("textures/shown.dds", 1024, HoldTier::Shown);
            const TextureIndex other = world.texture("textures/other.dds", 256, HoldTier::Shown);
            world.frames(3);
            ASSERT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(2));

            world.mDevice.setCap(2 * sChunk);
            world.frames(3);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(2)) << "raised with no chunk to spare";

            world.mDevice.setCap(3 * sChunk);
            world.frames(3);
            EXPECT_EQ(world.standingOf(shown), std::optional<std::uint8_t>(0));
            EXPECT_EQ(world.standingOf(other), std::optional<std::uint8_t>(0));
        }

        /// **What fits nowhere is starved and named**: under a cap of nought every shown texture's floor is starved,
        /// the report counts both, and nothing stands.
        TEST(RtxResidencyTest, aFloorThatFitsNowhereIsStarvedAndNamed)
        {
            World world(0);
            const TextureIndex one = world.texture("textures/one.dds", 256, HoldTier::Shown);
            const TextureIndex two = world.texture("textures/two.dds", 64, HoldTier::Shown);
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
            const TextureIndex first = world.texture("textures/first.dds", 256, HoldTier::Shown);
            world.frame();
            ASSERT_EQ(world.mFeed.getAsked().size(), 1u);
            const std::uint32_t generation = world.residentOf(first).mGeneration;

            world.mScene.drop(std::move(world.mTextures.back()));
            world.mTextures.pop_back();
            const TextureIndex second = world.texture("textures/second.dds", 128, HoldTier::Shown);
            ASSERT_EQ(second, first) << "the slot taken again";
            world.frame();
            EXPECT_EQ(world.residentOf(second).mGeneration, generation + 1);
            EXPECT_EQ(countOps<StandImage>(world.mDevice.getLastOps()), 0u) << "the old tenant's texels stood";
            ASSERT_EQ(world.mFeed.getAsked().size(), 2u);
            EXPECT_EQ(world.mFeed.getAsked()[1].mGeneration, generation + 1);

            world.frame();
            EXPECT_EQ(world.standingOf(second), std::optional<std::uint8_t>(0));
            const auto& stood = std::get<StandImage>(world.mDevice.getLastOps()[0]);
            EXPECT_EQ(stood.mUpload, (LevelSpan{ .mFirst = 0, .mEnd = 8 })) << "the 128² tenant's eight levels";
        }

        /// **A structure is built loose, then copied tight once the device answers its size**; one that refits is kept
        /// as built. A shown quad's mesh: its build on the second frame, the chunk asked on the first; the device's
        /// answer the frame after, and its tight copy then, the loose slot given back.
        TEST(RtxResidencyTest, aStructureIsBuiltLooseThenCopiedTight)
        {
            World world(1024 * sMiB);
            const MeshIndex mesh = Testing::addQuadMesh(world.mScene);
            world.mMeshes.push_back(world.mScene.holdMesh(mesh, HoldTier::Shown));
            world.frame();
            world.frame();
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

        /// **Texels that fail make the texture failed, and what stood of it is given back**: stood two levels down
        /// beside a 256² under a cap of a chunk, its raise under three asks the feed, which fails it. The texture is
        /// failed, planned at nothing, and the next hand-over drops its old slot; it is asked of the feed no more.
        TEST(RtxResidencyTest, texelsThatFailGiveBackWhatStood)
        {
            World world(sChunk);
            const TextureIndex texture = world.texture("textures/broken.dds", 1024, HoldTier::Shown);
            world.texture("textures/other.dds", 256, HoldTier::Shown);
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

        /// **A dropped scene gives back every slot as its last hand-over rides**, and a scene that takes its place
        /// stands its content again in slots the device saw retire: two textures and a structure stand; the drop hands
        /// the device three drops, and the scene added again stands all three anew with no slot stood twice.
        TEST(RtxResidencyTest, aDroppedSceneGivesBackItsSlots)
        {
            World world(1024 * sMiB);
            world.texture("textures/a.dds", 256, HoldTier::Shown);
            world.texture("textures/b.dds", 64, HoldTier::Shown);
            world.mMeshes.push_back(world.mScene.holdMesh(Testing::addQuadMesh(world.mScene), HoldTier::Shown));
            world.frames(3);
            ASSERT_TRUE(world.mResidency.isSettled(HoldTier::Shown));

            world.mResidency.dropScene(world.handing());
            EXPECT_EQ(countOps<DropImage>(world.mDevice.getLastOps()), 2u);
            EXPECT_EQ(countOps<DropStructure>(world.mDevice.getLastOps()), 1u);

            world.mResidency.addScene(SceneSlot::world());
            world.frames(4);
            EXPECT_TRUE(world.mResidency.isSettled(HoldTier::Shown));
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
                        const auto side = std::uint32_t{ 32 } << (random() % 7);
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
                    if (const auto* stand = std::get_if<StandImage>(&op); stand != nullptr && stand->mFeed.has_value())
                        lastUpload = world.mResidency.getLedger().getStrideBytes(
                            world.mResidency.getLedger().getClassOf(stand->mTo));
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
