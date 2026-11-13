#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Material>
#include <osg/PrimitiveSet>
#include <osg/StateSet>

#include <components/sceneutil/optimizer.hpp>

namespace
{
    osg::ref_ptr<osg::Geometry> triangleWearing(osg::ref_ptr<osg::StateSet> state, float x)
    {
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(x, 0.0f, 0.0f));
        vertices->push_back(osg::Vec3f(x + 1.0f, 0.0f, 0.0f));
        vertices->push_back(osg::Vec3f(x, 1.0f, 0.0f));
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
        geometry->setStateSet(state);
        return geometry;
    }

    osg::ref_ptr<osg::StateSet> stateOf(const osg::Vec4f& diffuse)
    {
        osg::ref_ptr<osg::Material> material = new osg::Material;
        material->setDiffuse(osg::Material::FRONT_AND_BACK, diffuse);
        osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
        state->setAttributeAndModes(material);
        return state;
    }

    /// **Drawables merge by what their states hold, not by which object holds it**: two equal states as two
    /// objects merge as one would, since whether they are one object is the shared state cache's history, which
    /// moved from process to process; two states that differ do not.
    TEST(SceneUtilOptimizerTest, drawablesMergeByWhatTheirStatesHold)
    {
        const osg::Vec4f red(1.0f, 0.0f, 0.0f, 1.0f);
        osg::ref_ptr<osg::Group> equal = new osg::Group;
        equal->addChild(triangleWearing(stateOf(red), 0.0f));
        equal->addChild(triangleWearing(stateOf(red), 2.0f));

        osg::ref_ptr<osg::Group> differ = new osg::Group;
        differ->addChild(triangleWearing(stateOf(red), 0.0f));
        differ->addChild(triangleWearing(stateOf(osg::Vec4f(0.0f, 1.0f, 0.0f, 1.0f)), 2.0f));

        SceneUtil::Optimizer optimizer;
        optimizer.optimize(equal, SceneUtil::Optimizer::MERGE_GEOMETRY);
        optimizer.optimize(differ, SceneUtil::Optimizer::MERGE_GEOMETRY);

        EXPECT_EQ(equal->getNumChildren(), 1u) << "two equal states as two objects";
        EXPECT_EQ(differ->getNumChildren(), 2u) << "two states that differ";
    }

    /// What a node may have done to it as the game's `Resource::SceneManager` permits it, which lets a group that
    /// wears a state be merged, where the optimizer's own rule leaves every such group alone.
    struct AsTheGamePermits final : SceneUtil::Optimizer::IsOperationPermissibleForObjectCallback
    {
        bool isOperationPermissibleForObjectImplementation(
            const SceneUtil::Optimizer*, const osg::Node*, unsigned int) const override
        {
            return true;
        }
    };

    /// **Groups merge by what their states hold too**, as drawables do: two child groups wearing equal states as two
    /// objects become one group holding both their children, and two wearing states that differ stay two.
    TEST(SceneUtilOptimizerTest, groupsMergeByWhatTheirStatesHold)
    {
        const auto groupWearing = [](osg::ref_ptr<osg::StateSet> state, float x) {
            osg::ref_ptr<osg::Group> group = new osg::Group;
            group->setStateSet(state);
            group->addChild(triangleWearing(nullptr, x));
            return group;
        };
        const osg::Vec4f red(1.0f, 0.0f, 0.0f, 1.0f);
        osg::ref_ptr<osg::Group> equal = new osg::Group;
        equal->addChild(groupWearing(stateOf(red), 0.0f));
        equal->addChild(groupWearing(stateOf(red), 2.0f));

        osg::ref_ptr<osg::Group> differ = new osg::Group;
        differ->addChild(groupWearing(stateOf(red), 0.0f));
        differ->addChild(groupWearing(stateOf(osg::Vec4f(0.0f, 1.0f, 0.0f, 1.0f)), 2.0f));

        SceneUtil::Optimizer optimizer;
        optimizer.setIsOperationPermissibleForObjectCallback(new AsTheGamePermits);
        optimizer.optimize(equal, SceneUtil::Optimizer::REMOVE_REDUNDANT_NODES);
        optimizer.optimize(differ, SceneUtil::Optimizer::REMOVE_REDUNDANT_NODES);

        ASSERT_EQ(equal->getNumChildren(), 1u) << "two equal states as two objects";
        ASSERT_NE(equal->getChild(0)->asGroup(), nullptr);
        EXPECT_EQ(equal->getChild(0)->asGroup()->getNumChildren(), 2u) << "both groups' children";
        EXPECT_EQ(differ->getNumChildren(), 2u) << "two states that differ";
    }
}
