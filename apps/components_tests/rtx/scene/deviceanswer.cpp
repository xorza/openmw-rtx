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
        /// of the four, handed to an answer that holds a stood mesh already, arrives behind it, and the answer it came
        /// from holds nothing. A forwarder that named the lists itself carried some of them
        /// (`DeviceScene::takeAnswer`).
        TEST(RtxDeviceAnswerTest, anAnswerHandedOnCarriesEveryListAndIsLeftEmpty)
        {
            DeviceAnswer from;
            from.mRefused.push_back(Refusal{ .mKind = Refused::Mesh, .mName = "name", .mWhy = "why" });
            from.mMeshes.push_back(StoodMesh{ .mMesh = MeshIndex(1), .mStructureBytes = 2 });
            from.mWants.push_back(RoomWant{ .mWaiter = MeshIndex(9), .mHole = 10, .mAlignment = 16 });
            from.mMeshFits.push_back(FitBytes<MeshIndex>{ .mSlot = MeshIndex(13), .mBytes = 14, .mAlignment = 2 });

            DeviceAnswer into;
            into.mMeshes.push_back(StoodMesh{ .mMesh = MeshIndex(0), .mStructureBytes = 1 });
            from.handTo(into);

            ASSERT_EQ(into.mRefused.size(), 1u);
            EXPECT_EQ(into.mRefused[0].mKind, Refused::Mesh);
            EXPECT_EQ(into.mRefused[0].mName, "name");
            EXPECT_EQ(into.mRefused[0].mWhy, "why");
            ASSERT_EQ(into.mMeshes.size(), 2u) << "what the taker held was not kept, or what was handed did not arrive";
            EXPECT_EQ(into.mMeshes[0].mMesh, MeshIndex(0));
            EXPECT_EQ(into.mMeshes[1].mMesh, MeshIndex(1));
            EXPECT_EQ(into.mMeshes[1].mStructureBytes, 2u);
            EXPECT_EQ(into.mWants, (std::vector{ RoomWant{ .mWaiter = MeshIndex(9), .mHole = 10, .mAlignment = 16 } }));
            EXPECT_EQ(into.mMeshFits,
                (std::vector{ FitBytes<MeshIndex>{ .mSlot = MeshIndex(13), .mBytes = 14, .mAlignment = 2 } }));

            EXPECT_TRUE(from.mRefused.empty());
            EXPECT_TRUE(from.mMeshes.empty());
            EXPECT_TRUE(from.mWants.empty());
            EXPECT_TRUE(from.mMeshFits.empty());
        }
    }
}
