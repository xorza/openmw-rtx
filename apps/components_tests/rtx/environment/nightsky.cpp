#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Math>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/PrimitiveSet>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/environment/nightsky.hpp>
#include <components/rtx/preprocess/threadcontent.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        /// A patch of sky around `towards`, unit, a tenth of a radian across: four corners, one
        /// sheet laid once over them, two triangles.
        osg::ref_ptr<osg::Geometry> patchAround(const osg::Vec3f& towards, const osg::Vec3f& side)
        {
            const osg::Vec3f up = towards ^ side;
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            osg::ref_ptr<osg::Vec2Array> coords = new osg::Vec2Array;
            for (const osg::Vec2f corner :
                { osg::Vec2f(-1, -1), osg::Vec2f(1, -1), osg::Vec2f(1, 1), osg::Vec2f(-1, 1) })
            {
                vertices->push_back((towards + side * (0.05f * corner.x()) + up * (0.05f * corner.y())) * 1000.0f);
                coords->push_back((corner + osg::Vec2f(1.0f, 1.0f)) * 0.5f);
            }

            osg::ref_ptr<osg::Geometry> patch = new osg::Geometry;
            patch->setVertexArray(vertices);
            patch->setTexCoordArray(0, coords, osg::Array::BIND_PER_VERTEX);
            osg::ref_ptr<osg::DrawElementsUShort> triangles = new osg::DrawElementsUShort(GL_TRIANGLES);
            for (const unsigned short index : { 0, 1, 2, 0, 2, 3 })
                triangles->push_back(index);
            patch->addPrimitiveSet(triangles);
            return patch;
        }

        /// A state set binding a sheet of `file` on the first unit: four grey texels, which the
        /// upload takes.
        osg::ref_ptr<osg::StateSet> binding(const std::string& file, unsigned int flags = osg::StateAttribute::ON)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            for (int at = 0; at < 16; ++at)
                image->data()[at] = 128;
            image->setFileName(file);

            osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
            state->setTextureAttributeAndModes(0, new osg::Texture2D(image), flags);
            return state;
        }

        /// **A patch is read where the graph places it and wears the sheet its path binds**, as the
        /// cloud shell and the atmosphere read theirs. The dome hangs under a quarter turn about
        /// `x`, so a patch modelled toward `y` stands overhead. Its sheet is bound two nodes above
        /// it; read off the drawable and its parent alone, the patch had none and was left out.
        /// And a second patch, under a parent that binds its sheet `OVERRIDE`, wears that sheet
        /// over its own, as the rasterizer's state stack resolves the two.
        TEST(RtxNightSkyTest, aPatchStandsWhereTheGraphPlacesItAndWearsTheSheetItsPathBinds)
        {
            const osg::Matrix turn = osg::Matrix::rotate(osg::PI_2, osg::X_AXIS);
            osg::ref_ptr<osg::MatrixTransform> dome = new osg::MatrixTransform(turn);

            osg::ref_ptr<osg::Group> painted = new osg::Group;
            painted->setStateSet(binding("textures/nebula.dds"));
            osg::ref_ptr<osg::Group> between = new osg::Group;
            between->addChild(patchAround(osg::Vec3f(0.0f, 1.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f)));
            painted->addChild(between);
            dome->addChild(painted);

            osg::ref_ptr<osg::Group> locked = new osg::Group;
            locked->setStateSet(
                binding("textures/warrior.dds", osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE));
            osg::ref_ptr<osg::Geometry> own = patchAround(osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f));
            own->setStateSet(binding("textures/thief.dds"));
            locked->addChild(own);
            dome->addChild(locked);

            SceneDesc scene;
            ThreadContent thread;
            std::vector<TextureHold> holds;

            // Given back however the test ends, since a hold left standing aborts the binary.
            struct Given
            {
                SceneDesc& mScene;
                std::vector<TextureHold>& mHolds;
                ~Given()
                {
                    for (TextureHold& hold : mHolds)
                        mScene.drop(std::move(hold));
                }
            } given{ scene, holds };

            const NightSky sky = readNightSky(scene, *dome, thread, holds);

            const NightSky::Patch& overhead = sky.mPatches[0];
            ASSERT_NE(overhead.mTexture, sNoIndex) << "the sheet two nodes up was not found";
            EXPECT_EQ(
                scene.textures().getRows()[overhead.mTexture].mPath, VFS::Path::NormalizedView("textures/nebula.dds"));

            const osg::Vec3f placed = osg::Vec3f(0.0f, 1.0f, 0.0f) * turn;
            EXPECT_GT(placed.z(), 0.999f) << "the turn this test means stands the patch overhead";
            EXPECT_NEAR(overhead.mDirection.x(), placed.x(), 1e-5f);
            EXPECT_NEAR(overhead.mDirection.y(), placed.y(), 1e-5f);
            EXPECT_NEAR(overhead.mDirection.z(), placed.z(), 1e-5f)
                << "read where it was modelled and not where it stands";

            // A corner is `atan(0.05 * sqrt 2)` off the middle, whatever turn the dome stands under.
            EXPECT_NEAR(overhead.mAngularRadius, std::atan(0.05f * std::sqrt(2.0f)), 1e-5f);

            const NightSky::Patch& overridden = sky.mPatches[1];
            ASSERT_NE(overridden.mTexture, sNoIndex);
            EXPECT_EQ(scene.textures().getRows()[overridden.mTexture].mPath,
                VFS::Path::NormalizedView("textures/warrior.dds"))
                << "the parent's OVERRIDE gave way to the drawable's own";
        }
    }
}
