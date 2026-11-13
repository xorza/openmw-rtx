#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Geometry>
#include <osg/Group>

#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/attach.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

namespace
{
    osg::ref_ptr<SceneUtil::RigGeometry> rigNamed(const std::string& name)
    {
        osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
        source->setVertexArray(new osg::Vec3Array(3));
        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
        rig->setSourceGeometry(source);
        rig->setName(name);
        return rig;
    }

    /// **A skinned part's pieces join the skeleton in the order the part lists them**, whatever their addresses:
    /// the five are listed from the highest address down, which a set of pointers turned the other way round, and
    /// which part's pieces stood where in the ray tracer's tables then moved from run to run.
    TEST(SceneUtilAttachTest, aSkinnedPartsPiecesJoinInTheOrderThePartListsThem)
    {
        const VFS::Manager vfs;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(&vfs, 1.0, &encoder.getStatelessEncoder());

        std::vector<osg::ref_ptr<SceneUtil::RigGeometry>> pieces;
        for (const char* name : { "gauntlet a", "gauntlet b", "gauntlet c", "gauntlet d", "gauntlet e" })
            pieces.push_back(rigNamed(name));
        std::ranges::sort(pieces, [](const auto& left, const auto& right) { return left.get() > right.get(); });

        osg::ref_ptr<SceneUtil::Skeleton> part = new SceneUtil::Skeleton;
        for (const osg::ref_ptr<SceneUtil::RigGeometry>& piece : pieces)
            part->addChild(piece);

        osg::ref_ptr<osg::Group> master = new SceneUtil::Skeleton;
        const osg::ref_ptr<osg::Node> attached
            = SceneUtil::attach(part, master, "gauntlet", master, resources.getSceneManager());
        const osg::Group* handle = attached->asGroup();
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(handle->getNumChildren(), pieces.size());
        for (unsigned int at = 0; at < handle->getNumChildren(); ++at)
            EXPECT_EQ(handle->getChild(at)->getName(), pieces[at]->getName()) << "piece " << at;
    }
}
