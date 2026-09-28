#include <algorithm>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3d>
#include <osg/Vec3f>

#include <apps/rtxtool/model/camerapath.hpp>
#include <apps/rtxtool/model/cameratrack.hpp>

namespace RtxTool
{
    namespace
    {
        TrackKey keyAt(const osg::Vec3f& eye, bool rests = false)
        {
            return TrackKey{ .mEye = eye, .mRests = rests };
        }

        /// **Along a line, a length is the distance.** Centripetal Catmull-Rom through eyes on one
        /// line stays on it, and runs forward while its tangents are under three times the chord:
        /// at 1000 between an end mirrored to −1000 and the eye at 3000, they are 1000 and
        /// `(31.6 − 39.3 + 44.7) · 31.6 = 1171`. So the legs are 1000 and 2000 long, and the point
        /// half way along each is its middle.
        TEST(RtxCameraPathTest, alongALineALengthIsTheDistance)
        {
            const std::vector<TrackKey> keys{ keyAt(osg::Vec3f(0, 0, 0)), keyAt(osg::Vec3f(1000, 0, 0)),
                keyAt(osg::Vec3f(3000, 0, 0)) };
            const CameraPath path(keys);

            ASSERT_EQ(path.getSegments(), 2u);
            EXPECT_NEAR(path.getLength(0), 1000.0, 1e-6);
            EXPECT_NEAR(path.getLength(1), 2000.0, 1e-6);
            EXPECT_NEAR(path.at(0, 500.0).x(), 500.0, 1e-5);
            EXPECT_NEAR(path.at(1, 1000.0).x(), 2000.0, 1e-5);
            EXPECT_EQ(path.at(1, 2000.0), osg::Vec3d(3000, 0, 0)) << "the last eye, to the bit";
        }

        /// **Round a corner the length is the curve's, and equal lengths are equal steps.** Three
        /// eyes at a right angle: the curve is longer than its chords and shorter than the two legs
        /// of the corner it cuts, and ten thousand points at even lengths along it stand an even
        /// ten-thousandth apart — against a polyline of them, which is a measurement of the length
        /// by other means, short of the curve only by what a chord cuts off its arc.
        TEST(RtxCameraPathTest, roundACornerEqualLengthsAreEqualSteps)
        {
            const std::vector<TrackKey> keys{ keyAt(osg::Vec3f(0, 0, 0)), keyAt(osg::Vec3f(1000, 0, 0)),
                keyAt(osg::Vec3f(1000, 1000, 0)) };
            const CameraPath path(keys);

            for (std::size_t segment = 0; segment < 2; ++segment)
            {
                const double length = path.getLength(segment);
                EXPECT_GT(length, 1000.0) << "the curve bows out from its chord";
                EXPECT_LT(length, 1100.0);

                constexpr std::size_t steps = 10000;
                const double even = length / static_cast<double>(steps);
                double polyline = 0.0;
                double widest = 0.0;
                double narrowest = even * 2.0;
                osg::Vec3d before = path.at(segment, 0.0);
                for (std::size_t at = 1; at <= steps; ++at)
                {
                    const osg::Vec3d point = path.at(segment, even * static_cast<double>(at));
                    const double stride = (point - before).length();
                    polyline += stride;
                    widest = std::max(widest, stride);
                    narrowest = std::min(narrowest, stride);
                    before = point;
                }

                EXPECT_NEAR(polyline, length, 1e-4) << "segment " << segment;
                EXPECT_NEAR(widest, even, 1e-5) << "segment " << segment;
                EXPECT_NEAR(narrowest, even, 1e-5) << "segment " << segment;
            }
        }

        /// **Smooth through an eye it passes, broken where it stands.** Through the corner's middle
        /// eye the direction a hair before is the direction a hair after. A resting key, a take's
        /// ends and both sides of a hold stand still, and a leg between two rests leaves and
        /// reaches them along its chord, so a straight one is exactly as long as it is far. A
        /// segment that goes nowhere is nought long and keeps its eye.
        TEST(RtxCameraPathTest, smoothThroughAnEyeItPassesBrokenWhereItStands)
        {
            const std::vector<TrackKey> corner{ keyAt(osg::Vec3f(0, 0, 0)), keyAt(osg::Vec3f(1000, 0, 0)),
                keyAt(osg::Vec3f(1000, 1000, 0)) };
            const CameraPath smooth(corner);
            const double before = smooth.getLength(0);
            osg::Vec3d in = smooth.at(0, before) - smooth.at(0, before - 1e-3);
            osg::Vec3d out = smooth.at(1, 1e-3) - smooth.at(1, 0.0);
            in.normalize();
            out.normalize();
            EXPECT_NEAR(in * out, 1.0, 1e-6) << "the direction through the middle eye";
            EXPECT_FALSE(smooth.restsAt(1));
            EXPECT_TRUE(smooth.restsAt(0));
            EXPECT_TRUE(smooth.restsAt(2));

            const std::vector<TrackKey> held{ keyAt(osg::Vec3f(0, 0, 0)), keyAt(osg::Vec3f(1000, 0, 0), true),
                keyAt(osg::Vec3f(1000, 0, 0), true), keyAt(osg::Vec3f(1000, 1000, 0)) };
            const CameraPath stops(held);
            ASSERT_EQ(stops.getSegments(), 3u);
            EXPECT_NEAR(stops.getLength(0), 1000.0, 1e-6) << "from rest to rest along the chord";
            EXPECT_EQ(stops.getLength(1), 0.0);
            EXPECT_EQ(stops.at(1, 0.0), osg::Vec3d(1000, 0, 0));
            EXPECT_NEAR(stops.getLength(2), 1000.0, 1e-6);
            for (std::size_t key = 0; key < 4; ++key)
                EXPECT_TRUE(stops.restsAt(key)) << "key " << key;

            const std::vector<TrackKey> resting{ keyAt(osg::Vec3f(0, 0, 0)), keyAt(osg::Vec3f(1000, 0, 0), true),
                keyAt(osg::Vec3f(1000, 1000, 0)) };
            EXPECT_NEAR(CameraPath(resting).getLength(0), 1000.0, 1e-6) << "a resting key breaks the corner";
            EXPECT_GT(before, 1000.0 + 1.0) << "and a passing one rounds it";
        }
    }
}
