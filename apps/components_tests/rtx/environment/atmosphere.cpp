#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include <osg/Geometry>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/environment/atmosphere.hpp>
#include <components/rtx/shaders/sky.h>
#include <components/sky/vertexrules.hpp>

namespace Rtx
{
    namespace
    {
        /// An atmosphere the shape of Morrowind's own: sixteen vertices at a radius of 1466 and
        /// sixteen at 1587, listed alternately, `low` and `high` units down, under the root rotation
        /// `diag(1, -1, -1)` that hangs the file's cylinder upside down. Even vertices make the
        /// upper ring, by the engine's rule.
        osg::ref_ptr<osg::Node> makeAtmosphere(float high, float low)
        {
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            for (int step = 0; step < 16; ++step)
            {
                const float angle = 2.0f * std::numbers::pi_v<float> * float(step) / 16.0f;
                vertices->push_back(osg::Vec3f(1466.0f * std::cos(angle), 1466.0f * std::sin(angle), -high));
                vertices->push_back(osg::Vec3f(1587.0f * std::cos(angle), 1587.0f * std::sin(angle), -low));
            }

            osg::ref_ptr<osg::Geometry> wall = new osg::Geometry;
            wall->setVertexArray(vertices);

            osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform(osg::Matrix::scale(1.0, -1.0, -1.0));
            root->addChild(wall);
            return root;
        }

        /// The share at `degrees` of elevation.
        float shareAt(const Shaders::SkyRamp& ramp, float degrees)
        {
            const float elevation = degrees * std::numbers::pi_v<float> / 180.0f;
            return Shaders::skyShare(ramp, osg::Vec3f(std::cos(elevation), 0.0f, std::sin(elevation)));
        }

        /// **Morrowind's atmosphere fades the fog colour to the sky colour between 3.6° and 28.6°.**
        /// The lower ring stands at `atan(100 / 1587)` = 3.606° and the upper at `atan(800 / 1466)` =
        /// 28.621°, and the share up the wall between them is `(1587 s - 100 c) / (700 c + 121 s)`:
        /// at 10°, `(275.58 - 98.48) / (689.37 + 21.01)` = 0.2493; at 16.4°, 0.4990; just under the
        /// upper ring, 0.9991; and all of it above. The sky colour a surface facing the sky receives,
        /// integrated in closed form, is 0.9076 of the whole, where the gradient this replaced, linear
        /// in the sine, gave two thirds.
        TEST(RtxAtmosphereTest, theFadeIsTheMeshesAndSoIsWhatItIsWorth)
        {
            const Atmosphere read = readAtmosphere(*makeAtmosphere(800.0f, 100.0f));
            EXPECT_NEAR(read.mRamp.mBottom, std::sin(3.606f * std::numbers::pi_v<float> / 180.0f), 1e-4f);
            EXPECT_NEAR(read.mRamp.mTop, std::sin(28.621f * std::numbers::pi_v<float> / 180.0f), 1e-4f);

            EXPECT_EQ(shareAt(read.mRamp, 0.0f), 0.0f) << "the fog colour at the horizon";
            EXPECT_EQ(shareAt(read.mRamp, 3.5f), 0.0f) << "and up to the lower ring";
            EXPECT_NEAR(shareAt(read.mRamp, 10.0f), 0.2493f, 1e-4f);
            EXPECT_NEAR(shareAt(read.mRamp, 16.4f), 0.4990f, 1e-4f);
            EXPECT_NEAR(shareAt(read.mRamp, 28.6f), 0.9991f, 1e-4f);
            EXPECT_EQ(shareAt(read.mRamp, 45.0f), 1.0f) << "the cap fanned shut above the upper ring";
            EXPECT_EQ(shareAt(read.mRamp, 90.0f), 1.0f);

            // The closed form against the share itself, summed over the hemisphere: `2 ∫ z t dz` at
            // the middle of a hundred thousand bands.
            constexpr int bands = 100000;
            double summed = 0.0;
            for (int band = 0; band < bands; ++band)
            {
                const double up = (band + 0.5) / bands;
                const float elevation = float(std::asin(up));
                summed += 2.0 * up
                    * double{ Shaders::skyShare(
                        read.mRamp, osg::Vec3f(std::cos(elevation), 0.0f, std::sin(elevation))) };
            }
            EXPECT_NEAR(read.mZenithShare, 0.9076f, 1e-4f);
            EXPECT_NEAR(read.mZenithShare, summed / bands, 1e-5);

            // A lower upper ring is a steeper fade, which is the mesh read and not a constant.
            const Atmosphere steeper = readAtmosphere(*makeAtmosphere(400.0f, 100.0f));
            EXPECT_LT(steeper.mRamp.mTop, read.mRamp.mTop);
            EXPECT_GT(shareAt(steeper.mRamp, 10.0f), shareAt(read.mRamp, 10.0f));
            EXPECT_GT(steeper.mZenithShare, read.mZenithShare);
        }

        /// **A mesh that is not the engine's cylinder is no atmosphere**, and the sky is the fog colour
        /// everywhere, as the rasterizer's is with none to draw. A fresh one is the same. A frame
        /// built by hand, all nought, draws the sky colour above the horizon.
        TEST(RtxAtmosphereTest, aMeshThatIsNotACylinderOfTwoRingsIsNone)
        {
            const Atmosphere fresh{};
            EXPECT_EQ(shareAt(fresh.mRamp, 90.0f), 0.0f);
            EXPECT_EQ(fresh.mZenithShare, 0.0f);

            const Atmosphere upsideDown = readAtmosphere(*makeAtmosphere(100.0f, 800.0f));
            EXPECT_EQ(shareAt(upsideDown.mRamp, 45.0f), 0.0f) << "the faded ring above the full one";
            EXPECT_EQ(upsideDown.mZenithShare, 0.0f);

            const osg::ref_ptr<osg::Node> nothing = new osg::MatrixTransform;
            const Atmosphere empty = readAtmosphere(*nothing);
            EXPECT_EQ(shareAt(empty.mRamp, 45.0f), 0.0f);

            const Shaders::SkyRamp byHand{};
            EXPECT_EQ(shareAt(byHand, 0.0f), 1.0f);
            EXPECT_EQ(shareAt(byHand, -10.0f), 0.0f);
        }

        /// The engine's rule for which ring a vertex stands in, read by both renderers.
        TEST(RtxAtmosphereTest, everySecondVertexStandsInTheFadedRing)
        {
            EXPECT_EQ(Sky::atmosphereAlphaOf(0), 1.0f);
            EXPECT_EQ(Sky::atmosphereAlphaOf(1), 0.0f);
            EXPECT_EQ(Sky::atmosphereAlphaOf(30), 1.0f);
            EXPECT_EQ(Sky::atmosphereAlphaOf(31), 0.0f);
        }
    }
}
