#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/GL>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/PolygonMode>
#include <osg/PrimitiveSet>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>

#include <apps/openmw/mwrender/rtx/debugwalk.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/rtx/frame/debuglines.hpp>

namespace MWRender
{
    namespace
    {
        /// The view mask the game culls its world by.
        constexpr unsigned int sView = ~(Mask_UpdateVisitor | Mask_SimpleWater);

        /// A geometry of `positions`, painted `colours` per vertex where there are as many, or the
        /// first over the whole where there is one, drawn as `mode`.
        osg::ref_ptr<osg::Geometry> drawn(
            const std::vector<osg::Vec3f>& positions, const std::vector<osg::Vec4f>& colours, const GLenum mode)
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(new osg::Vec3Array(positions.begin(), positions.end()));
            geometry->setColorArray(new osg::Vec4Array(colours.begin(), colours.end()),
                colours.size() == positions.size() ? osg::Array::BIND_PER_VERTEX : osg::Array::BIND_OVERALL);
            geometry->addPrimitiveSet(new osg::DrawArrays(mode, 0, static_cast<int>(positions.size())));
            return geometry;
        }

        /// The walk reads every debug geometry under the root by the transform in force at it,
        /// takes a strip and a quad apart into lines and triangles, keeps each vertex's own colour
        /// or the one over the whole, and enters nothing that is not a debug node. A colour's alpha
        /// is read only under `GL_BLEND`, as the rasterizer reads it: the cell borders paint at an
        /// alpha of nought and draw opaque, and the navmesh blends.
        ///
        /// **Under a node of `Mask_Debug` alone, culled by the view's mask**, as the rasterizer's
        /// cull draws them: a scene root beside the debug nodes holds the world, and a walk that
        /// read it would read every mesh in the cell as a line drawing. A border's line under a
        /// `Mask_Terrain` group is read where the view keeps the ground, and not under `tws`,
        /// which takes it; the ground's own geometry beside it is no debug node and is never read.
        TEST(RtxDebugWalkTest, theWalkReadsDebugGeometryByItsTransformAndNothingElse)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;

            // A pathgrid's lines, moved a hundred along x, under the debug mask.
            osg::ref_ptr<osg::MatrixTransform> moved
                = new osg::MatrixTransform(osg::Matrix::translate(100.0, 0.0, 0.0));
            moved->setNodeMask(Mask_Debug);
            moved->addChild(
                drawn({ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 10.0f, 0.0f), osg::Vec3f(0.0f, 20.0f, 0.0f) },
                    { osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f), osg::Vec4f(0.0f, 1.0f, 0.0f, 0.0f),
                        osg::Vec4f(0.0f, 0.0f, 1.0f, 1.0f) },
                    GL_LINE_STRIP));
            root->addChild(moved);

            // A navmesh's quad, one colour over the whole, straight under the root.
            osg::ref_ptr<osg::Geometry> quad = drawn({ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f),
                                                         osg::Vec3f(1.0f, 1.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f) },
                { osg::Vec4f(0.5f, 0.5f, 0.5f, 0.25f) }, GL_QUADS);
            quad->setNodeMask(Mask_Debug);
            quad->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);
            root->addChild(quad);

            // The scene, which is not a debug node and is not read.
            osg::ref_ptr<osg::Geometry> scene = drawn(
                { osg::Vec3f(), osg::Vec3f(), osg::Vec3f() }, { osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f) }, GL_TRIANGLES);
            scene->setNodeMask(Mask_Scene);
            root->addChild(scene);

            // A cell border under the terrain's group, beside the ground's own mesh.
            osg::ref_ptr<osg::Group> terrain = new osg::Group;
            terrain->setNodeMask(Mask_Terrain);
            osg::ref_ptr<osg::Geometry> border = drawn({ osg::Vec3f(0.0f, 0.0f, 5.0f), osg::Vec3f(8.0f, 0.0f, 5.0f) },
                { osg::Vec4f(1.0f, 1.0f, 0.0f, 1.0f) }, GL_LINES);
            border->setNodeMask(Mask_Debug);
            terrain->addChild(border);
            terrain->addChild(drawn(
                { osg::Vec3f(), osg::Vec3f(), osg::Vec3f() }, { osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f) }, GL_TRIANGLES));

            osg::ref_ptr<osg::Group> bordered = new osg::Group;
            bordered->addChild(terrain);
            DebugWalk walk;
            const Rtx::DebugLines shown = walk.walk(*bordered, sView);
            ASSERT_EQ(shown.mLines.size(), 2u) << "the border under the ground the view keeps";
            EXPECT_EQ(shown.mLines[1].mPosition, osg::Vec3f(8.0f, 0.0f, 5.0f));
            EXPECT_TRUE(shown.mTriangles.empty()) << "the ground's own mesh is no debug node";
            EXPECT_TRUE(walk.walk(*bordered, sView & ~Mask_Terrain).empty()) << "a border under `tws`";

            const Rtx::DebugLines lines = walk.walk(*root, sView);

            // A strip of three is two lines of two vertices, each where the transform put it and
            // each its own colour, opaque where nothing blends.
            ASSERT_EQ(lines.mLines.size(), 4u);
            EXPECT_EQ(lines.mLines[0].mPosition, osg::Vec3f(100.0f, 0.0f, 0.0f));
            EXPECT_EQ(lines.mLines[1].mPosition, osg::Vec3f(100.0f, 10.0f, 0.0f));
            EXPECT_EQ(lines.mLines[2].mPosition, osg::Vec3f(100.0f, 10.0f, 0.0f));
            EXPECT_EQ(lines.mLines[3].mPosition, osg::Vec3f(100.0f, 20.0f, 0.0f));
            EXPECT_EQ(lines.mLines[0].mColour, osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f));
            EXPECT_EQ(lines.mLines[1].mColour, osg::Vec4f(0.0f, 1.0f, 0.0f, 1.0f));
            EXPECT_EQ(lines.mLines[3].mColour, osg::Vec4f(0.0f, 0.0f, 1.0f, 1.0f));

            // A quad is two triangles, and the scene's triangle is not among them. It blends, so its
            // alpha stands.
            ASSERT_EQ(lines.mTriangles.size(), 6u);
            EXPECT_EQ(lines.mTriangles[0].mPosition, osg::Vec3f(0.0f, 0.0f, 0.0f));
            EXPECT_EQ(lines.mTriangles[2].mPosition, osg::Vec3f(1.0f, 1.0f, 0.0f));
            EXPECT_EQ(lines.mTriangles[5].mPosition, osg::Vec3f(0.0f, 1.0f, 0.0f));
            for (const Rtx::DebugVertex& vertex : lines.mTriangles)
                EXPECT_EQ(vertex.mColour, osg::Vec4f(0.5f, 0.5f, 0.5f, 0.25f));

            // And the next walk starts over rather than adding to the last.
            const Rtx::DebugLines again = walk.walk(*root, sView);
            EXPECT_EQ(again.mLines.size(), 4u);
            EXPECT_EQ(again.mTriangles.size(), 6u);

            // With no debug node under it, a root answers nothing.
            osg::ref_ptr<osg::Group> plain = new osg::Group;
            plain->addChild(scene);
            EXPECT_TRUE(walk.walk(*plain, sView).empty());
        }

        /// **Under `PolygonMode::LINE` a polygon is its edges**, as the collision drawer's shapes
        /// draw under `tcb`: a triangle three lines and a quad its four sides with no diagonal,
        /// fourteen vertices and no face. A `FILL` stated further down puts faces back, as the
        /// rasterizer's state stack does: the quad below it is two triangles again.
        TEST(RtxDebugWalkTest, aPolygonUnderLineModeIsItsEdges)
        {
            osg::ref_ptr<osg::Group> shapes = new osg::Group;
            shapes->setNodeMask(Mask_Debug);
            shapes->getOrCreateStateSet()->setAttributeAndModes(
                new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE));

            const osg::Vec4f white(1.0f, 1.0f, 1.0f, 1.0f);
            shapes->addChild(
                drawn({ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f) },
                    { white }, GL_TRIANGLES));
            const std::vector<osg::Vec3f> corners{ osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(1.0f, 0.0f, 1.0f),
                osg::Vec3f(1.0f, 1.0f, 1.0f), osg::Vec3f(0.0f, 1.0f, 1.0f) };
            shapes->addChild(drawn(corners, { white }, GL_QUADS));

            osg::ref_ptr<osg::Geometry> filled = drawn(corners, { white }, GL_QUADS);
            filled->getOrCreateStateSet()->setAttributeAndModes(
                new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::FILL));
            shapes->addChild(filled);

            DebugWalk walk;
            const Rtx::DebugLines lines = walk.walk(*shapes, sView);

            ASSERT_EQ(lines.mLines.size(), 14u) << "three edges and four sides";
            EXPECT_EQ(lines.mLines[4].mPosition, osg::Vec3f(0.0f, 1.0f, 0.0f)) << "the triangle closes";
            EXPECT_EQ(lines.mLines[5].mPosition, osg::Vec3f(0.0f, 0.0f, 0.0f));
            EXPECT_EQ(lines.mLines[12].mPosition, osg::Vec3f(0.0f, 1.0f, 1.0f)) << "the quad closes, no diagonal";
            EXPECT_EQ(lines.mLines[13].mPosition, osg::Vec3f(0.0f, 0.0f, 1.0f));
            EXPECT_EQ(lines.mTriangles.size(), 6u) << "the filled quad, and nothing of the two above it";
        }
    }
}
