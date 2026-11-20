#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/refusal.hpp>

namespace Rtx
{
    namespace
    {
        /// **An answer handed on carries every list, after what its taker held, and is left empty**: one entry in each
        /// of the seven, handed to an answer that holds a moved texture already, arrives behind it, and the answer it
        /// came from holds nothing. A forwarder that named the lists itself carried four of the seven
        /// (`DeviceScene::takeAnswer`).
        TEST(RtxDeviceAnswerTest, anAnswerHandedOnCarriesEveryListAndIsLeftEmpty)
        {
            DeviceAnswer from;
            from.mRefused.push_back(Refusal{ .mKind = Refused::Texture, .mName = "name", .mWhy = "why" });
            from.mMeshes.push_back(StoodMesh{ .mMesh = MeshIndex(1), .mStructureBytes = 2 });
            from.mShort.push_back(ShortTexture{ .mSlot = TextureIndex(3), .mStoodBytes = 4, .mWholeBytes = 5 });
            from.mMoved.push_back(MovedTexture{ .mSlot = TextureIndex(6), .mStoodBytes = 7, .mFrom = 8 });
            from.mWants.push_back(RoomWant{ .mWaiter = MeshIndex(9), .mHole = 10, .mAlignment = 16 });
            from.mTextureFits.push_back(
                FitBytes<TextureIndex>{ .mSlot = TextureIndex(11), .mBytes = 12, .mAlignment = 4 });
            from.mMeshFits.push_back(FitBytes<MeshIndex>{ .mSlot = MeshIndex(13), .mBytes = 14, .mAlignment = 2 });

            DeviceAnswer into;
            into.mMoved.push_back(MovedTexture{ .mSlot = TextureIndex(0), .mStoodBytes = 1, .mFrom = 1 });
            from.handTo(into);

            ASSERT_EQ(into.mRefused.size(), 1u);
            EXPECT_EQ(into.mRefused[0].mKind, Refused::Texture);
            EXPECT_EQ(into.mRefused[0].mName, "name");
            EXPECT_EQ(into.mRefused[0].mWhy, "why");
            ASSERT_EQ(into.mMeshes.size(), 1u);
            EXPECT_EQ(into.mMeshes[0].mMesh, MeshIndex(1));
            EXPECT_EQ(into.mMeshes[0].mStructureBytes, 2u);
            EXPECT_EQ(into.mShort,
                (std::vector{ ShortTexture{ .mSlot = TextureIndex(3), .mStoodBytes = 4, .mWholeBytes = 5 } }));
            ASSERT_EQ(into.mMoved.size(), 2u) << "what the taker held was not kept, or what was handed did not arrive";
            EXPECT_EQ(into.mMoved[0].mSlot, TextureIndex(0));
            EXPECT_EQ(into.mMoved[1].mSlot, TextureIndex(6));
            EXPECT_EQ(into.mMoved[1].mStoodBytes, 7u);
            EXPECT_EQ(into.mMoved[1].mFrom, 8u);
            EXPECT_EQ(into.mWants, (std::vector{ RoomWant{ .mWaiter = MeshIndex(9), .mHole = 10, .mAlignment = 16 } }));
            EXPECT_EQ(into.mTextureFits,
                (std::vector{ FitBytes<TextureIndex>{ .mSlot = TextureIndex(11), .mBytes = 12, .mAlignment = 4 } }));
            EXPECT_EQ(into.mMeshFits,
                (std::vector{ FitBytes<MeshIndex>{ .mSlot = MeshIndex(13), .mBytes = 14, .mAlignment = 2 } }));

            EXPECT_TRUE(from.mRefused.empty());
            EXPECT_TRUE(from.mMeshes.empty());
            EXPECT_TRUE(from.mShort.empty());
            EXPECT_TRUE(from.mMoved.empty());
            EXPECT_TRUE(from.mWants.empty());
            EXPECT_TRUE(from.mTextureFits.empty());
            EXPECT_TRUE(from.mMeshFits.empty());
        }
    }
}
