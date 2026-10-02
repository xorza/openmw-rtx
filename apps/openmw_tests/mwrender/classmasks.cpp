#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/classmasks.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/scene.h>

namespace MWRender
{
    namespace
    {
        /// **What a view's mask says to the trace.** The local map's inclusion mask is the ground,
        /// the buildings and the still water: its rays meet the statics and the water and nothing
        /// else, and no lamp lights it, because it leaves out `Mask_Lighting`. The game's own eye,
        /// which leaves nothing out, meets every class, draws the sprites, and is lit by every lamp.
        TEST(MWRenderClassMasksTest, aViewsMaskSaysWhichClassesItMeetsAndWhetherLampsLightIt)
        {
            const ViewDescription map
                = describeView(Mask_Scene | Mask_SimpleWater | Mask_Terrain | Mask_Object | Mask_Static);
            EXPECT_EQ(map.mRayMask, Rtx::classBit(Rtx::InstanceClass::Static) | Rtx::Shaders::MASK_WATER);
            EXPECT_FALSE(map.mLamps);

            const ViewDescription eye = describeView(~0u);
            EXPECT_EQ(eye.mRayMask,
                Rtx::classBit(Rtx::InstanceClass::Static) | Rtx::classBit(Rtx::InstanceClass::Actor)
                    | Rtx::classBit(Rtx::InstanceClass::Effect) | Rtx::classBit(Rtx::InstanceClass::FirstPerson)
                    | Rtx::Shaders::MASK_WATER | Rtx::Shaders::MASK_PARTICLE);
            EXPECT_TRUE(eye.mLamps);
        }
    }
}
