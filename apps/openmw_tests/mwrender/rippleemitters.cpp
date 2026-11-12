#include <span>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/rippleemitters.hpp>
#include <apps/openmw/mwrender/sceneframe.hpp>
#include <components/rtx/scene/ripple.hpp>

namespace MWRender
{
    namespace
    {
        constexpr WaterState sWater{ .mHeight = 100.0f, .mEnabled = true, .mToggled = true };

        /// **A strike rings the water where it landed, near enough the surface**: within twenty units
        /// of it, above or below, a ring of twelve at its place on the water; 19 under it rings and 20
        /// over it does not. Pressed once, on the frame after it.
        TEST(RtxRippleEmittersTest, aStrikeNearTheSurfaceRingsWhereItLandedOnce)
        {
            RippleEmitters emitters;
            emitters.splash(osg::Vec3f(10.0f, -5.0f, 81.0f));
            emitters.splash(osg::Vec3f(30.0f, 40.0f, 120.0f));
            emitters.update(sWater, true);

            const std::span<const Rtx::RippleImpulse> rings = emitters.getImpulses();
            ASSERT_EQ(rings.size(), 1u);
            EXPECT_EQ(rings[0].mAt, osg::Vec2f(10.0f, -5.0f));
            EXPECT_EQ(rings[0].mSize, 12.0f);

            emitters.update(sWater, true);
            EXPECT_TRUE(emitters.getImpulses().empty()) << "a strike rang twice";
        }

        /// **A frame the simulation stood still on presses nothing and keeps its strikes** for the
        /// frame it runs again; hidden water drops them, as a world not drawn does (`dropStrikes`).
        TEST(RtxRippleEmittersTest, aStillFrameKeepsItsStrikesAndHiddenWaterDropsThem)
        {
            RippleEmitters emitters;
            emitters.splash(osg::Vec3f(1.0f, 2.0f, 100.0f));
            emitters.update(sWater, false);
            EXPECT_TRUE(emitters.getImpulses().empty());
            emitters.update(sWater, true);
            EXPECT_EQ(emitters.getImpulses().size(), 1u) << "the still frame lost its strike";

            emitters.splash(osg::Vec3f(1.0f, 2.0f, 100.0f));
            emitters.update(WaterState{ .mHeight = 100.0f, .mEnabled = true, .mToggled = false }, true);
            EXPECT_TRUE(emitters.getImpulses().empty());
            emitters.update(sWater, true);
            EXPECT_TRUE(emitters.getImpulses().empty()) << "hidden water kept a strike";

            emitters.splash(osg::Vec3f(1.0f, 2.0f, 100.0f));
            emitters.dropStrikes();
            emitters.update(sWater, true);
            EXPECT_TRUE(emitters.getImpulses().empty()) << "a dropped strike rang";
        }
    }
}
