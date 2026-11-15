#include <components/files/configurationmanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/resource/sptfilemanager.hpp>
#include <components/sceneutil/material.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/spt/file.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>

#include <osg/Drawable>
#include <osg/NodeVisitor>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    using namespace testing;

    TEST(ResourceResourceSystem, scenemanager_getinstance_should_be_thread_safe)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        const Files::ConfigurationManager configurationManager;
        sceneManager->setShaderPath(configurationManager.getLocalPath() / "resources/shaders");

        auto defines = Shader::getDefaultDefines();

        auto shadowDefines = SceneUtil::ShadowManager::getShadowsDisabledDefines();

        osg::ref_ptr<SceneUtil::LightManager> lightManager
            = new SceneUtil::LightManager(SceneUtil::LightSettings{}, &resourceSystem);
        auto lightDefines = lightManager->getLightDefines();

        for (const auto& define : shadowDefines)
            defines[define.first] = define.second;
        for (const auto& define : lightDefines)
            defines[define.first] = define.second;

        sceneManager->getShaderManager().setGlobalDefines(defines);

        constexpr VFS::Path::NormalizedView noSuchPath("meshes/whatever.nif");
        std::vector<std::thread> threads;

        threads.reserve(50);
        for (int i = 0; i < 50; ++i)
        {
            threads.emplace_back([=]() { sceneManager->getInstance(noSuchPath); });
        }
        for (std::thread& thread : threads)
            thread.join();
    }

    /// **A path let go of is loaded again**: a template asked twice is the one the cache kept, and asked
    /// after `letGo` names its path, a new one, while one another path names stays.
    TEST(ResourceResourceSystem, scenemanager_letgo_drops_the_paths_it_names)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        sceneManager->setModelPrograms(Resource::ModelPrograms::Omitted);

        constexpr VFS::Path::NormalizedView first("meshes/first.nif");
        constexpr VFS::Path::NormalizedView kept("meshes/kept.nif");
        const osg::ref_ptr<const osg::Node> loaded = sceneManager->getTemplate(first);
        const osg::ref_ptr<const osg::Node> keptLoaded = sceneManager->getTemplate(kept);
        EXPECT_EQ(sceneManager->getTemplate(first), loaded) << "the cache's own";

        const std::vector<std::string> letGo{ std::string(first.value()) };
        sceneManager->letGo(letGo);
        EXPECT_NE(sceneManager->getTemplate(first), loaded) << "loaded again";
        EXPECT_EQ(sceneManager->getTemplate(kept), keptLoaded) << "a path not named stays";
    }

    /// A SpeedTree file of a header and nothing after it, which `Spt::parse` reads: its opening section,
    /// the version it takes, and the section that ends the header, each little-endian.
    std::string headerOnlySpeedTree()
    {
        std::string bytes;
        const auto append = [&bytes](std::uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<char>((value >> shift) & 0xffu));
        };
        constexpr std::string_view version = "__IdvSpt_02_";
        append(1000);
        append(static_cast<std::uint32_t>(version.size()));
        bytes += version;
        append(1001);
        return bytes;
    }

    /// **A SpeedTree file let go of is read again**: `letGo` drops the parsed `.spt` its path names, as it
    /// drops a NIF file, so the preparation's memory does not keep every tree it read, while one another
    /// path names stays.
    TEST(ResourceResourceSystem, scenemanager_letgo_drops_the_speedtree_files_it_names)
    {
        TestingOpenMW::VFSTestFile firstFile(headerOnlySpeedTree());
        TestingOpenMW::VFSTestFile keptFile(headerOnlySpeedTree());
        constexpr VFS::Path::NormalizedView first("trees/first.spt");
        constexpr VFS::Path::NormalizedView kept("trees/kept.spt");
        const std::unique_ptr<VFS::Manager> vfs
            = TestingOpenMW::createTestVFS({ { first, &firstFile }, { kept, &keptFile } });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        Resource::SptFileManager& trees = *resourceSystem.getSptFileManager();

        const Spt::SPTFilePtr read = trees.get(first);
        const Spt::SPTFilePtr keptRead = trees.get(kept);
        EXPECT_EQ(read->mHeader.mVersionId, "__IdvSpt_02_");
        EXPECT_EQ(trees.get(first), read) << "the cache's own";

        const std::vector<std::string> letGo{ std::string(first.value()) };
        resourceSystem.getSceneManager()->letGo(letGo);
        EXPECT_NE(trees.get(first), read) << "read again";
        EXPECT_EQ(trees.get(kept), keptRead) << "a path not named stays";
    }

    /// Every `SceneUtil::Material` a scene's state sets hold, by its name.
    class MaterialsByName : public osg::NodeVisitor
    {
    public:
        MaterialsByName()
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        {
        }

        void apply(osg::Node& node) override
        {
            take(node.getStateSet());
            traverse(node);
        }

        void apply(osg::Drawable& drawable) override { take(drawable.getStateSet()); }

        std::map<std::string, osg::ref_ptr<const SceneUtil::Material>> mFound;

    private:
        void take(const osg::StateSet* stateSet)
        {
            if (stateSet == nullptr)
                return;
            if (const auto* material
                = dynamic_cast<const SceneUtil::Material*>(stateSet->getAttribute(osg::StateAttribute::MATERIAL)))
                mFound[material->getName()] = material;
        }
    };

    /// **The error marker keeps its material**: it names `SceneUtil::Material`, whose `.osgt` wrapper the scene
    /// manager registers as it is made, so a model loaded where there is none is the marker `ErrorMarker::sValue`
    /// states, its material's every value as written there.
    TEST(ResourceResourceSystem, scenemanager_error_marker_keeps_its_material)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        sceneManager->setModelPrograms(Resource::ModelPrograms::Omitted);

        constexpr VFS::Path::NormalizedView missing("meshes/missing.nif");
        const osg::ref_ptr<const osg::Node> marker = sceneManager->getTemplate(missing);
        ASSERT_NE(marker, nullptr);
        MaterialsByName materials;
        // `accept` takes a node a visitor may change, and this one only reads.
        const_cast<osg::Node&>(*marker).accept(materials);

        ASSERT_EQ(materials.mFound.count("Error"), 1u) << "the marker lost its material";
        const SceneUtil::Material& error = *materials.mFound.at("Error");
        EXPECT_EQ(error.getAmbient(), osg::Vec4f(1.0f, 1.0f, 1.0f, 0.5f));
        EXPECT_EQ(error.getDiffuse(), osg::Vec4f(0.8f, 0.704f, 0.32f, 0.5f));
        EXPECT_EQ(error.getSpecular(), osg::Vec4f(0.5f, 0.5f, 0.5f, 0.5f));
        EXPECT_EQ(error.getEmission(), osg::Vec4f(1.0f, 0.88f, 0.4f, 0.5f));
        EXPECT_EQ(error.getShininess(), 28.8f);
    }
}
