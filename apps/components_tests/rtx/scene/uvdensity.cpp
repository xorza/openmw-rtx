#include <array>
#include <limits>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <components/rtx/scene/uvdensity.hpp>

namespace Rtx
{
    namespace
    {
        /// **The sparsest triangle of each set is the mesh's**, where the shader reads finest. The unit quad's two
        /// triangles each span twice an area of 1 in its own units. The first set lays v0..v3 at (0,0), (1,0), (1,1),
        /// (0,3): the first triangle spans |(1,0) × (1,1)| = 1 of it, the second |(1,1) × (0,3)| = 3, so 1. The second
        /// set stretches the quad by 2 across and by 4 up, |(2,0) × (2,4)| = |(2,4) × (0,4)| = 8 for both triangles,
        /// answered apart from the first set's.
        TEST(RtxUvDensityTest, theSparsestTriangleOfEachSetIsTheMeshs)
        {
            const std::array first{ osg::Vec2f(0.0f, 0.0f), osg::Vec2f(1.0f, 0.0f), osg::Vec2f(1.0f, 1.0f),
                osg::Vec2f(0.0f, 3.0f) };
            const std::array second{ osg::Vec2f(0.0f, 0.0f), osg::Vec2f(2.0f, 0.0f), osg::Vec2f(2.0f, 4.0f),
                osg::Vec2f(0.0f, 4.0f) };

            EXPECT_EQ(uvDensityOf(Testing::sUnitQuad, first, second, Testing::sQuadIndices),
                (UvDensity{ .mFirst = 1.0f, .mSecond = 8.0f }));

            // The positions matter as the coordinates do: the quad doubled each way spans four times the area, and
            // lays a quarter of the texture on each unit of it.
            std::array<osg::Vec3f, 4> doubled = Testing::sUnitQuad;
            for (osg::Vec3f& corner : doubled)
                corner *= 2.0f;
            EXPECT_EQ(uvDensityOf(doubled, first, second, Testing::sQuadIndices),
                (UvDensity{ .mFirst = 0.25f, .mSecond = 2.0f }));
        }

        /// **What the shader reads at its finest level is nought**, as `coneBaseOf` returns its sentinel or a level of
        /// minus infinity: a triangle with no area in the world (three corners on one line), one with none in its
        /// coordinates (all on one texel), and every triangle of a set the mesh does not bring. A mesh of no triangles
        /// reads nothing, infinite; one whose maker stated none asks the finest.
        TEST(RtxUvDensityTest, whatTheShaderReadsAtItsFinestIsNought)
        {
            const std::array coords{ osg::Vec2f(0.0f, 0.0f), osg::Vec2f(1.0f, 0.0f), osg::Vec2f(1.0f, 1.0f),
                osg::Vec2f(0.0f, 1.0f) };

            const std::array line{ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(2.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f) };
            EXPECT_EQ(uvDensityOf(line, coords, {}, Testing::sQuadIndices).mFirst, 0.0f)
                << "the first triangle, v0 v1 v2, spans no area in the world";

            const std::array oneTexel{ osg::Vec2f(0.5f, 0.5f), osg::Vec2f(0.5f, 0.5f), osg::Vec2f(0.5f, 0.5f),
                osg::Vec2f(0.5f, 0.5f) };
            EXPECT_EQ(uvDensityOf(Testing::sUnitQuad, coords, oneTexel, Testing::sQuadIndices),
                (UvDensity{ .mFirst = 1.0f, .mSecond = 0.0f }));

            EXPECT_EQ(uvDensityOf(Testing::sUnitQuad, coords, {}, Testing::sQuadIndices),
                (UvDensity{ .mFirst = 1.0f, .mSecond = 0.0f }))
                << "a set the mesh does not bring reads nought everywhere";

            constexpr float none = std::numeric_limits<float>::infinity();
            EXPECT_EQ(uvDensityOf(Testing::sUnitQuad, coords, {}, {}), (UvDensity{ .mFirst = none, .mSecond = none }));
            EXPECT_EQ(UvDensity{}, (UvDensity{ .mFirst = 0.0f, .mSecond = 0.0f }));
        }
    }
}
