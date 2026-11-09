#include <components/files/configurationmanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
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
        sceneManager->setShadersEnabled(false);

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
}
