#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/ref_ptr>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace
{
    /// A document of one triangle under a node named `name`, padded with a comment of `padding` bytes.
    std::string colladaTriangle(const std::string& name, std::size_t padding)
    {
        return R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <!-- )" + std::string(padding, 'x')
            + R"( -->
  <asset><unit name="meter" meter="1"/><up_axis>Z_UP</up_axis></asset>
  <library_geometries>
    <geometry id="mesh" name="mesh">
      <mesh>
        <source id="positions">
          <float_array id="positions-array" count="9">0 0 0 1 0 0 0 1 0</float_array>
          <technique_common>
            <accessor source="#positions-array" count="3" stride="3">
              <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="vertices"><input semantic="POSITION" source="#positions"/></vertices>
        <triangles count="1"><input semantic="VERTEX" source="#vertices" offset="0"/><p>0 1 2</p></triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="scene" name="scene">
      <node id=")"
            + name + R"(" name=")" + name + R"("><instance_geometry url="#mesh"/></node>
    </visual_scene>
  </library_visual_scenes>
  <scene><instance_visual_scene url="#scene"/></scene>
</COLLADA>
)";
    }

    /// How many vertices the geometry under `root` holds: three for a triangle, and the error
    /// marker's many where the model failed to load.
    std::size_t vertexCount(const osg::Node& root)
    {
        struct Counter : osg::NodeVisitor
        {
            Counter()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            void apply(osg::Drawable& drawable) override
            {
                if (const osg::Geometry* geometry = drawable.asGeometry())
                    if (const osg::Array* vertices = geometry->getVertexArray())
                        mCount += vertices->getNumElements();
            }

            std::size_t mCount = 0;
        } counter;
        const_cast<osg::Node&>(root).accept(counter);
        return counter.mCount;
    }

    /// OSG's COLLADA reader reads a stream's bytes as a C string, so a short document read after a
    /// long one parsed on into what the long one left on the heap, and failed. Each model loads,
    /// whatever was read before it.
    TEST(ResourceSceneManager, aColladaModelLoadsWhateverWasReadBeforeIt)
    {
        TestingOpenMW::VFSTestFile large(colladaTriangle("large", 60000));
        TestingOpenMW::VFSTestFile small(colladaTriangle("small", 0));
        constexpr VFS::Path::NormalizedView largePath("meshes/large.dae");
        constexpr VFS::Path::NormalizedView smallPath("meshes/small.dae");
        const auto vfs = TestingOpenMW::createTestVFS({ { largePath, &large }, { smallPath, &small } });

        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager& scenes = *resources.getSceneManager();
        scenes.setShadersEnabled(false);

        EXPECT_EQ(vertexCount(*scenes.getTemplate(largePath, false)), 3u);
        EXPECT_EQ(vertexCount(*scenes.getTemplate(smallPath, false)), 3u)
            << "the short document after the long one loaded as the error marker";
    }
}
