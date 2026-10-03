#include <gtest/gtest.h>

#include <components/rtx/scene/light.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx::Shaders
{
    namespace
    {
        /// **A light's traits are its fill bit and, a byte up, its class bits**, and a view shows it
        /// where its ray mask keeps any one of them: a static's lamp goes dark under `tws`, which
        /// keeps every class but the statics', and an actor's stays. A light made by hand answers to
        /// every class, so a test's lamp lights whatever it is shot under.
        TEST(RtxLightTraitsTest, aLightIsShownWhereTheViewKeepsAClassItAnswersTo)
        {
            EXPECT_EQ(lightTraits(true, MASK_EFFECT), 0x2001u) << "the effects' 0x20 a byte up, and the fill";
            EXPECT_EQ(lightFill(lightTraits(true, MASK_EFFECT)), 1.0f);
            EXPECT_EQ(lightFill(lightTraits(false, MASK_EVERY_CLASS)), 0.0f);

            const uint withoutStatics = MASK_EVERY_CLASS & ~MASK_STATIC;
            EXPECT_EQ(lightShown(lightTraits(false, MASK_STATIC), MASK_EVERY_CLASS), 1.0f);
            EXPECT_EQ(lightShown(lightTraits(false, MASK_STATIC), withoutStatics), 0.0f);
            EXPECT_EQ(lightShown(lightTraits(false, MASK_ACTOR), withoutStatics), 1.0f);
            EXPECT_EQ(lightShown(lightTraits(true, MASK_STATIC), withoutStatics), 0.0f) << "the fill bit is no class";

            EXPECT_EQ(Light{}.mTraits, lightTraits(false, MASK_EVERY_CLASS));
            EXPECT_EQ(lightShown(Light{}.mTraits, MASK_ACTOR), 1.0f);
        }
    }
}
