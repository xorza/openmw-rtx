#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/LOD>
#include <osg/Material>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Sequence>
#include <osg/StateSet>
#include <osg/Switch>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/graph.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/mirror/cells/nightday.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/rtx/mirror/cells/templatewalk.hpp>
#include <components/rtx/mirror/meshreader.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/sceneutil/extradata.hpp>
#include <components/shader/automaps.hpp>

namespace Rtx::Testing
{
    namespace
    {
        /// The walk reaches what the frame's walk reaches of a model that stands still, and hands
        /// each drawable the chain and the transform the frame would have composed for it.
        TEST(RtxTemplateWalkTest, aTemplateIsWalkedByTheFrameWalksRulesForWhatStandsStill)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            osg::StateSet* rootState = root->getOrCreateStateSet();

            // A colour on the root, so a part read under it shows the root's state set was in
            // force at the drawable.
            osg::ref_ptr<osg::Material> tint = new osg::Material;
            tint->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4f(0.25f, 0.5f, 0.75f, 1.0f));
            rootState->setAttribute(tint);

            // Ten units along x, over everything below.
            osg::ref_ptr<osg::MatrixTransform> moved
                = new osg::MatrixTransform(osg::Matrix::translate(10.0f, 0.0f, 0.0f));
            root->addChild(moved);

            // A switch with its first branch off: the harvested plant, the night lamp at noon.
            osg::ref_ptr<osg::Switch> branches = new osg::Switch;
            osg::ref_ptr<osg::Geometry> off = makeQuad();
            osg::ref_ptr<osg::Geometry> on = makeQuad();
            on->getOrCreateStateSet();
            branches->addChild(off, false);
            branches->addChild(on, true);
            moved->addChild(branches);

            // A flipbook standing on its second frame.
            osg::ref_ptr<osg::Sequence> frames = new osg::Sequence;
            osg::ref_ptr<osg::Geometry> first = makeQuad();
            osg::ref_ptr<osg::Geometry> second = makeQuad();
            frames->addChild(first);
            frames->addChild(second);
            frames->setValue(1);
            moved->addChild(frames);

            // An LOD stands its nearest level, the far one first so that the answer is not the
            // first child: a ray is owed the finest, as the frame walk takes it.
            osg::ref_ptr<osg::LOD> levels = new osg::LOD;
            osg::ref_ptr<osg::Geometry> near = makeQuad();
            osg::ref_ptr<osg::Geometry> far = makeQuad();
            levels->addChild(far, 100.0f, 1000.0f);
            levels->addChild(near, 0.0f, 100.0f);
            moved->addChild(levels);

            // The near level carries tangents, and the only ones: its part's run of them starts the
            // model's.
            osg::ref_ptr<osg::Vec4Array> tangents = new osg::Vec4Array;
            for (const float handedness : { 1.0f, -1.0f, 1.0f, -1.0f })
                tangents->push_back(osg::Vec4f(1.0f, 0.0f, 0.0f, handedness));
            near->setTexCoordArray(Shader::sTangentUnit, tangents, osg::Array::BIND_PER_VERTEX);

            // What the loader hid, under a mask the walk is told to keep out of.
            constexpr osg::Node::NodeMask hidden = 0x1;
            osg::ref_ptr<osg::Geometry> collision = makeQuad();
            collision->setNodeMask(hidden);
            root->addChild(collision);

            // A heat haze, which draws into the rasterizer's distortion buffer alone.
            osg::ref_ptr<osg::Group> haze = new osg::Group;
            SceneUtil::setupDistortion(*haze, SceneUtil::DistortionConfig{});
            osg::ref_ptr<osg::Geometry> hazed = makeQuad();
            haze->addChild(hazed);
            root->addChild(haze);

            PreparedModel model;
            TemplateWalk walk(nullptr);
            walk.read(*root, ~hidden, model);

            ASSERT_EQ(model.mParts.size(), 3u) << "the branch that is on, the frame shown, and the near level";
            EXPECT_EQ(model.mPositions.size(), 12u) << "three quads' corners, appended in turn";

            EXPECT_EQ(model.mParts[0].mDrawable, on.get());
            ASSERT_EQ(model.chainOf(model.mParts[0]).size(), 1u);
            EXPECT_EQ(model.chainOf(model.mParts[0])[0], rootState)
                << "the drawable's own state set states nothing, and the root's is the chain's one link";
            ASSERT_TRUE(model.mParts[0].mMaterial.mDescribed.has_value());
            EXPECT_EQ(model.mParts[0].mMaterial.mDescribed->mDiffuseColour, (EncodedColour{ 0.25f, 0.5f, 0.75f }))
                << "and the root's state set was in force at it";
            EXPECT_EQ(osg::Vec3f() * model.mParts[0].mLocal, osg::Vec3f(10.0f, 0.0f, 0.0f))
                << "moved by the transform above it";
            EXPECT_EQ(model.mParts[0].mVertices, (Rtx::Run{ .mOffset = 0, .mCount = 4 }));

            EXPECT_EQ(model.mParts[1].mDrawable, second.get()) << "the frame the sequence stands on, unstepped";
            ASSERT_EQ(model.chainOf(model.mParts[1]).size(), 1u);
            EXPECT_EQ(model.chainOf(model.mParts[1])[0], rootState) << "nearest last, and the root is all there is";
            EXPECT_EQ(model.mParts[1].mVertices, (Rtx::Run{ .mOffset = 4, .mCount = 4 }));

            EXPECT_EQ(model.mParts[2].mDrawable, near.get());
            EXPECT_EQ(model.mParts[0].mTangents.mCount, 0u);
            EXPECT_EQ(model.mParts[1].mTangents.mCount, 0u);
            EXPECT_EQ(model.mParts[2].mTangents, (Rtx::Run{ .mOffset = 0, .mCount = 4 }));
            const MeshReading nearReading = model.readingOf(model.mParts[2]);
            ASSERT_EQ(nearReading.mArrays.mTangents.size(), 4u);
            for (std::size_t vertex = 0; vertex < 4; ++vertex)
                EXPECT_EQ(nearReading.mArrays.mTangents[vertex], (*tangents)[vertex]) << vertex;

            for (const PreparedPart& part : model.mParts)
            {
                EXPECT_NE(part.mDrawable, off.get()) << "a branch that is off is not in the world";
                EXPECT_NE(part.mDrawable, first.get()) << "a frame the flipbook is not on is not shown";
                EXPECT_NE(part.mDrawable, far.get()) << "a level for a farther eye is not the finest";
                EXPECT_NE(part.mDrawable, collision.get()) << "what the loader hid is not walked";
                EXPECT_NE(part.mDrawable, hazed.get()) << "a heat haze is traced";
            }

            // **Nothing was stepped.** A frame's walk moves a flipbook's clock; this one may not,
            // because the template is every clone's and every thread's.
            EXPECT_EQ(frames->getValue(), 1);
        }

        /// The modes named, one bit each.
        NightDayModes modesOf(std::initializer_list<NightDayMode> shown)
        {
            NightDayModes modes{ .mBits = 0 };
            for (const NightDayMode mode : shown)
                modes = modes | NightDayModes::only(mode);
            return modes;
        }

        /// **Every branch of a `NightDaySwitch` is read**, under the modes that show it, and a quad
        /// outside the switch under every mode. The game's mode `m` shows child `m` of a switch of
        /// more than `m` children and child 0 of any other (`DayNightCallback`), and `Authored` the
        /// child the file opens on:
        /// - two children, opening on 1: child 0 in `Default` and `InteriorDay` (two is not more
        ///   than two), child 1 in `ExteriorNight` and `Authored`;
        /// - four, opening on 0: each of the first three in the mode of its index, child 0 in
        ///   `Authored` too, and child 3 in none, so it is not read;
        /// - two opening on 0, inside child 1 of two opening on 0, which shows in `ExteriorNight`
        ///   alone: the inner child 0 would show in `Default`, `InteriorDay` and `Authored`, and
        ///   meets the outer's in none, so it is not read; the inner child 1 in `ExteriorNight`.
        TEST(RtxTemplateWalkTest, everyBranchOfADayNightSwitchIsReadUnderTheModesThatShowIt)
        {
            using enum NightDayMode;

            // **Every mode is the four of them, counted from the enum**: a mode added and left out
            // of a hand-written mask would read every unswitched part as switched.
            EXPECT_EQ(modesOf({ Default, ExteriorNight, InteriorDay, Authored }).mBits, NightDayModes::sEvery);
            EXPECT_TRUE(modesOf({ Default, ExteriorNight, InteriorDay, Authored }).isEvery());
            EXPECT_FALSE(modesOf({ Default, ExteriorNight, InteriorDay }).isEvery());

            const auto dayNight = [](std::initializer_list<osg::Node*> children, unsigned int opensOn) {
                osg::ref_ptr<osg::Switch> branches = new osg::Switch;
                branches->setName(Constants::NightDayLabel);
                for (osg::Node* child : children)
                    branches->addChild(child, false);
                branches->setSingleChildOn(opensOn);
                return branches;
            };

            struct Read
            {
                const osg::Drawable* mDrawable = nullptr;
                NightDayModes mModes;
            };

            const osg::ref_ptr<osg::Geometry> quads[] = { makeQuad(), makeQuad(), makeQuad(), makeQuad() };
            const osg::ref_ptr<osg::Group> outerNight = new osg::Group;
            outerNight->addChild(dayNight({ quads[2], quads[3] }, 0));

            const std::pair<osg::ref_ptr<osg::Switch>, std::vector<Read>> cases[] = {
                { dayNight({ quads[0], quads[1] }, 1),
                    { { quads[0], modesOf({ Default, InteriorDay }) },
                        { quads[1], modesOf({ ExteriorNight, Authored }) } } },
                { dayNight({ quads[0], quads[1], quads[2], quads[3] }, 0),
                    { { quads[0], modesOf({ Default, Authored }) }, { quads[1], modesOf({ ExteriorNight }) },
                        { quads[2], modesOf({ InteriorDay }) } } },
                { dayNight({ quads[0], outerNight }, 0),
                    { { quads[0], modesOf({ Default, InteriorDay, Authored }) },
                        { quads[3], modesOf({ ExteriorNight }) } } },
            };

            TemplateWalk walk(nullptr);
            for (std::size_t at = 0; at < std::size(cases); ++at)
            {
                const auto& [branches, read] = cases[at];
                const osg::ref_ptr<osg::Geometry> plain = makeQuad();
                osg::ref_ptr<osg::Group> root = new osg::Group;
                root->addChild(plain);
                root->addChild(branches);

                PreparedModel model;
                walk.read(*root, ~0u, model);
                ASSERT_EQ(model.mParts.size(), 1 + read.size()) << at;
                EXPECT_EQ(model.mParts[0].mDrawable, plain.get()) << at;
                EXPECT_TRUE(model.mParts[0].mModes.isEvery()) << at << ": outside the switch";
                for (std::size_t part = 0; part < read.size(); ++part)
                {
                    EXPECT_EQ(model.mParts[1 + part].mDrawable, read[part].mDrawable) << at << ", " << part;
                    EXPECT_EQ(model.mParts[1 + part].mModes, read[part].mModes) << at << ", " << part;
                }
            }
        }

        /// **A mesh this cannot build is left out on the reader's thread, and its model stands
        /// without it**, as the frame's walk stands a model without a drawable it refuses: the quad
        /// beside it is the model's one part, and the model says why the other went. A mesh past one
        /// block is one, and a triangle naming a vertex its drawable does not have is another. The
        /// walk after it is the next model's, with no reason carried into it.
        TEST(RtxTemplateWalkTest, aMeshThisCannotBuildIsLeftOutAndItsModelStands)
        {
            const std::string pastABlock = "its " + std::to_string(MeshTable::sVertexBlock + 1)
                + " vertices and 3 indices are past the " + std::to_string(MeshTable::sVertexBlock) + " and "
                + std::to_string(MeshTable::sIndexBlock) + " one block of the shared buffers holds";

            TemplateWalk walk(nullptr);
            for (const auto& [broken, why] : { std::pair{ makePastOneBlock(), pastABlock },
                     std::pair{ makeIndexPastItsVertices(), std::string("its triangles name vertex 4 of 4") } })
            {
                osg::ref_ptr<osg::Group> root = new osg::Group;
                const osg::ref_ptr<osg::Geometry> kept = makeQuad();
                root->addChild(broken);
                root->addChild(kept);

                PreparedModel model;
                walk.read(*root, ~0u, model);
                EXPECT_EQ(model.mRefused, why);
                ASSERT_EQ(model.mParts.size(), 1u) << "the model was refused whole";
                EXPECT_EQ(model.mParts[0].mDrawable, kept.get());
                EXPECT_EQ(model.mPositions.size(), 4u) << "the refused mesh left arrays behind";

                const osg::ref_ptr<osg::Geometry> quad = makeQuad();
                PreparedModel next;
                walk.read(*quad, ~0u, next);
                EXPECT_TRUE(next.mRefused.empty()) << "a refusal carried into the next model";
                EXPECT_EQ(next.mParts.size(), 1u);
            }
        }
    }
}
