#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/frame/lampchanges.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    namespace
    {
        using Shaders::GpuLampChange;
        using Shaders::GpuLampSphere;

        Light lampAt(const osg::Vec3f& position, float reach = 100.0f, float source = 2.0f)
        {
            return Light{ .mPosition = position,
                .mIntensity = osg::Vec3f(1.0f, 1.0f, 1.0f),
                .mReach = reach,
                .mSourceRadius = source };
        }

        void expectSphere(const GpuLampSphere& sphere, const osg::Vec3f& centre, float reach, float source)
        {
            EXPECT_EQ(sphere.mCentre, centre);
            EXPECT_EQ(sphere.mReach, reach);
            EXPECT_EQ(sphere.mSource, source);
        }

        /// Spends what `lights` changed from nothing, so the next spend is held against them.
        void settle(LampChanges& changes, std::span<const Light> lights)
        {
            changes.see(lights);
            std::ignore = changes.spend(osg::Vec3f());
        }

        /// **Every lamp came on the first frame, and nothing changed on a frame whose lamps stand where they
        /// stood**, whatever order the walk lists them in: a lamp is its place, not its index. One that comes
        /// in front of the rest is one change, and renumbers nothing.
        TEST(RtxLampChangesTest, aLampIsItsPlaceAndNotItsIndex)
        {
            LampChanges changes;
            const std::array lamps{ lampAt(osg::Vec3f(10.0f, 0.0f, 0.0f)), lampAt(osg::Vec3f(0.0f, 300.0f, 0.0f)) };
            changes.see(lamps);
            const std::span<const GpuLampChange> first = changes.spend(osg::Vec3f());
            ASSERT_EQ(first.size(), 2u);
            expectSphere(first[0].mFrom, osg::Vec3f(10.0f, 0.0f, 0.0f), 0.0f, 2.0f);
            expectSphere(first[0].mTo, osg::Vec3f(10.0f, 0.0f, 0.0f), 100.0f, 2.0f);

            const std::array swapped{ lamps[1], lamps[0] };
            changes.see(swapped);
            EXPECT_TRUE(changes.spend(osg::Vec3f()).empty());

            const std::array joined{ lampAt(osg::Vec3f(0.0f, 0.0f, 500.0f)), lamps[1], lamps[0] };
            changes.see(joined);
            const std::span<const GpuLampChange> came = changes.spend(osg::Vec3f());
            ASSERT_EQ(came.size(), 1u);
            expectSphere(came[0].mFrom, osg::Vec3f(0.0f, 0.0f, 500.0f), 0.0f, 2.0f);
            expectSphere(came[0].mTo, osg::Vec3f(0.0f, 0.0f, 500.0f), 100.0f, 2.0f);

            changes.see(lamps);
            const std::span<const GpuLampChange> went = changes.spend(osg::Vec3f());
            ASSERT_EQ(went.size(), 1u);
            expectSphere(went[0].mFrom, osg::Vec3f(0.0f, 0.0f, 500.0f), 100.0f, 2.0f);
            expectSphere(went[0].mTo, osg::Vec3f(0.0f, 0.0f, 500.0f), 0.0f, 2.0f);
        }

        /// **A lamp that moved is one change from where it stood to where it stands, relative to the eye**,
        /// and a reach or a source that changed is a lamp that moved. A lamp that moved and moved back
        /// between two frames changed nothing: what counts is what the history saw and what the frame sees.
        TEST(RtxLampChangesTest, aLampThatMovedIsOneChangeRelativeToTheEye)
        {
            LampChanges changes;
            const std::array before{ lampAt(osg::Vec3f(10.0f, 0.0f, 0.0f)) };
            settle(changes, before);

            const std::array moved{ lampAt(osg::Vec3f(13.0f, 0.0f, 0.0f), 101.0f, 3.0f) };
            changes.see(moved);
            const std::span<const GpuLampChange> step = changes.spend(osg::Vec3f(1.0f, 2.0f, 3.0f));
            ASSERT_EQ(step.size(), 1u);
            expectSphere(step[0].mFrom, osg::Vec3f(9.0f, -2.0f, -3.0f), 100.0f, 2.0f);
            expectSphere(step[0].mTo, osg::Vec3f(12.0f, -2.0f, -3.0f), 101.0f, 3.0f);

            changes.see(before);
            changes.see(moved);
            EXPECT_TRUE(changes.spend(osg::Vec3f()).empty()) << "a frame's changes, not a placement's";

            changes.see(before);
            EXPECT_EQ(changes.spend(osg::Vec3f()).size(), 1u);
            EXPECT_TRUE(changes.spend(osg::Vec3f()).empty()) << "a frame nothing placed changed nothing";
        }

        /// **A lamp that went pairs with the nearest that came whose reach overlaps its own.** From 0, one came
        /// at 30 and one at 20, both overlapping: it moved to 20, and the one at 30 came. Two whose reaches do
        /// not meet, 0 to 250 with reaches of 100, are one that went and one that came, nearest the eye first.
        TEST(RtxLampChangesTest, aLampThatWentPairsWithTheNearestThatCameWithinReach)
        {
            LampChanges changes;
            settle(changes, std::array{ lampAt(osg::Vec3f()) });

            changes.see(std::array{ lampAt(osg::Vec3f(30.0f, 0.0f, 0.0f)), lampAt(osg::Vec3f(20.0f, 0.0f, 0.0f)) });
            const std::span<const GpuLampChange> paired = changes.spend(osg::Vec3f());
            ASSERT_EQ(paired.size(), 2u);
            expectSphere(paired[0].mFrom, osg::Vec3f(), 100.0f, 2.0f);
            expectSphere(paired[0].mTo, osg::Vec3f(20.0f, 0.0f, 0.0f), 100.0f, 2.0f);
            expectSphere(paired[1].mFrom, osg::Vec3f(30.0f, 0.0f, 0.0f), 0.0f, 2.0f);
            expectSphere(paired[1].mTo, osg::Vec3f(30.0f, 0.0f, 0.0f), 100.0f, 2.0f);

            settle(changes, std::array{ lampAt(osg::Vec3f()) });
            changes.see(std::array{ lampAt(osg::Vec3f(250.0f, 0.0f, 0.0f)) });
            const std::span<const GpuLampChange> apart = changes.spend(osg::Vec3f(400.0f, 0.0f, 0.0f));
            ASSERT_EQ(apart.size(), 2u);
            expectSphere(apart[0].mFrom, osg::Vec3f(-150.0f, 0.0f, 0.0f), 0.0f, 2.0f);
            expectSphere(apart[0].mTo, osg::Vec3f(-150.0f, 0.0f, 0.0f), 100.0f, 2.0f);
            expectSphere(apart[1].mFrom, osg::Vec3f(-400.0f, 0.0f, 0.0f), 100.0f, 2.0f);
            expectSphere(apart[1].mTo, osg::Vec3f(-400.0f, 0.0f, 0.0f), 0.0f, 2.0f);
        }

        /// **Past `LAMP_CHANGES_MOST` the nearest the eye are kept**, by where their light begins: twenty lamps
        /// coming at 1000 units apart with the eye at the last, the sixteen nearest it, nearest first.
        TEST(RtxLampChangesTest, pastTheMostTheNearestTheEyeAreKept)
        {
            LampChanges changes;
            std::vector<Light> lamps;
            lamps.reserve(20);
            for (std::size_t at = 0; at < 20; ++at)
                lamps.push_back(lampAt(osg::Vec3f(1000.0f * static_cast<float>(at), 0.0f, 0.0f)));
            changes.see(lamps);
            const std::span<const GpuLampChange> came = changes.spend(osg::Vec3f(19000.0f, 0.0f, 0.0f));
            ASSERT_EQ(came.size(), Shaders::LAMP_CHANGES_MOST);
            for (std::size_t at = 0; at < came.size(); ++at)
                EXPECT_EQ(came[at].mTo.mCentre, osg::Vec3f(-1000.0f * static_cast<float>(at), 0.0f, 0.0f)) << at;
        }

        /// **The share of a lamp's light a change changed, against its light without the window.**
        ///
        /// A lamp that came, of reach 100 and a source of one unit: all of it at its middle, and at half its
        /// reach the window's square, `(1 - 0.5^4)^2` = 0.87890625; nothing at its reach. A lamp that moved a
        /// unit, seen ten units off its old place and nine off its new: `(1/82 - 1/101) / (1/82)` = 19/101 =
        /// 0.188119, the window `1 - 10^-8` too near one to show in a float. A reach that grew from 100 to 101,
        /// seen at 50: `(1 - (50/101)^4)^2 - (1 - 0.5^4)^2` = 0.0045786, the little a breathing reach changes.
        TEST(RtxLampChangesTest, aChangeIsTheShareOfTheLampsLightItChanged)
        {
            const GpuLampSphere lamp{ .mCentre = osg::Vec3f(), .mReach = 100.0f, .mSource = 1.0f };
            const GpuLampSphere none{ .mCentre = osg::Vec3f(), .mReach = 0.0f, .mSource = 1.0f };
            const GpuLampChange came{ .mFrom = none, .mTo = lamp };
            EXPECT_EQ(Shaders::lampChangeShare(came, osg::Vec3f()), 1.0f);
            EXPECT_NEAR(Shaders::lampChangeShare(came, osg::Vec3f(50.0f, 0.0f, 0.0f)), 0.87890625f, 1e-6f);
            EXPECT_EQ(Shaders::lampChangeShare(came, osg::Vec3f(0.0f, 100.0f, 0.0f)), 0.0f);
            EXPECT_EQ(Shaders::lampChangeShare(GpuLampChange{ .mFrom = lamp, .mTo = none }, osg::Vec3f()), 1.0f)
                << "a lamp that went is a lamp that came, the other way";
            EXPECT_EQ(Shaders::lampChangeShare(GpuLampChange{ .mFrom = none, .mTo = none }, osg::Vec3f()), 0.0f);

            const GpuLampSphere far{ .mCentre = osg::Vec3f(), .mReach = 1000.0f, .mSource = 1.0f };
            const GpuLampSphere stepped{ .mCentre = osg::Vec3f(1.0f, 0.0f, 0.0f), .mReach = 1000.0f, .mSource = 1.0f };
            EXPECT_NEAR(
                Shaders::lampChangeShare(GpuLampChange{ .mFrom = far, .mTo = stepped }, osg::Vec3f(10.0f, 0.0f, 0.0f)),
                19.0f / 101.0f, 1e-6f);

            const GpuLampSphere grown{ .mCentre = osg::Vec3f(), .mReach = 101.0f, .mSource = 1.0f };
            EXPECT_NEAR(
                Shaders::lampChangeShare(GpuLampChange{ .mFrom = lamp, .mTo = grown }, osg::Vec3f(50.0f, 0.0f, 0.0f)),
                0.0045786f, 2e-6f);
        }
    }
}
