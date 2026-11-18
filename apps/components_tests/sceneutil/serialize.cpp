#include <sstream>

#include <gtest/gtest.h>

#include <osg/Group>
#include <osg/StateSet>
#include <osgDB/Options>
#include <osgDB/ReaderWriter>
#include <osgDB/Registry>

#include <components/sceneutil/material.hpp>
#include <components/sceneutil/serialize.hpp>

namespace
{
    /// **A material's four colours survive the scene format both ways**: written by the serializers the tree registers
    /// and read back, each the colour it was and none another's.
    TEST(SceneUtilSerializeTest, aMaterialsColoursRoundTripEachToItself)
    {
        SceneUtil::registerSerializers();
        const osgDB::ReaderWriter* format = osgDB::Registry::instance()->getReaderWriterForExtension("osgt");
        ASSERT_NE(format, nullptr);
        osg::ref_ptr<osgDB::Options> options = new osgDB::Options;
        options->setPluginStringData("fileType", "Ascii");

        const osg::Vec4f ambient(0.125f, 0.25f, 0.375f, 0.5f);
        const osg::Vec4f diffuse(0.5f, 0.625f, 0.75f, 0.875f);
        const osg::Vec4f specular(1.0f, 0.875f, 0.75f, 0.625f);
        const osg::Vec4f emission(0.0625f, 0.1875f, 0.3125f, 0.4375f);
        osg::ref_ptr<SceneUtil::Material> material = new SceneUtil::Material;
        material->setAmbient(ambient);
        material->setDiffuse(diffuse);
        material->setSpecular(specular);
        material->setEmission(emission);
        osg::ref_ptr<osg::Group> node = new osg::Group;
        node->getOrCreateStateSet()->setAttribute(material);

        std::stringstream stream;
        ASSERT_TRUE(format->writeNode(*node, stream, options).success());
        osgDB::ReaderWriter::ReadResult read = format->readNode(stream, options);
        ASSERT_TRUE(read.validNode());
        const osg::StateSet* state = read.getNode()->getStateSet();
        ASSERT_NE(state, nullptr);
        const auto* back = dynamic_cast<const SceneUtil::Material*>(state->getAttribute(osg::StateAttribute::MATERIAL));
        ASSERT_NE(back, nullptr);

        EXPECT_EQ(back->getAmbient(), ambient);
        EXPECT_EQ(back->getDiffuse(), diffuse);
        EXPECT_EQ(back->getSpecular(), specular);
        EXPECT_EQ(back->getEmission(), emission);
    }
}
