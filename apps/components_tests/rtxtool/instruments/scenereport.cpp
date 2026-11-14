#include <string>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/rtxtool/instruments/scenereport.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace RtxTool
{
    namespace
    {
        /// **What the walk found is said as it was counted**, and a second walk and the store each only where there
        /// was one: a walk that did not happen has no count, and a nought would read as one that resolved nothing.
        TEST(RtxSceneReportTest, aSecondWalkAndTheStoreAreSaidOnlyWhereThereWasOne)
        {
            const Rtx::SceneDesc scene;
            MWRender::WalkReport walked;
            walked.mFound.mDistantStatics = 7;
            walked.mFound.mGroundCells = 25;
            walked.mFound.mUndescribedSurfaces = 3;

            const std::string once = describeScene(scene, walked);
            EXPECT_NE(once.find("\nplaced\n  instances:            0\n  distant statics:      7\n"
                                "  ground cells:         25\n"),
                std::string::npos)
                << once;
            EXPECT_NE(once.find("  undescribed surfaces: 3 drawn as a default material\n"), std::string::npos) << once;
            EXPECT_EQ(once.find("second pass"), std::string::npos);
            EXPECT_EQ(once.find("  store "), std::string::npos);

            Rtx::ExtractionStats again;
            again.mMeshesAdded = 2;
            again.mRestood = 1;
            walked.mAgain = again;
            ContentStore::Stats store;
            store.mFound = 4;
            store.mMissed = 5;
            walked.mStore = store;
            const std::string twice = describeScene(scene, walked);
            EXPECT_NE(twice.find("\nsecond pass over the same graph\n  new meshes:           2 (should be 0)\n"),
                std::string::npos)
                << twice;
            EXPECT_NE(twice.find("  stood again:          1 (should be 0)\n"), std::string::npos) << twice;
            EXPECT_NE(twice.find("  store         4 found,       5 missed,"), std::string::npos) << twice;
            EXPECT_TRUE(twice.starts_with(once.substr(0, once.find("\npreprocessed"))))
                << "what the first walk found reads the same";
        }
    }
}
