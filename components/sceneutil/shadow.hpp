#pragma once

#include <cstdint>

#include <components/shader/shadermanager.hpp>

namespace osg
{
    class StateSet;
    class Group;
}

namespace osgShadow
{
    class ShadowSettings;
    class ShadowedScene;
}

namespace Settings
{
    struct ShadowsCategory;
}

namespace SceneUtil
{
    class MWShadowTechnique;
    class ShadowManager
    {
    public:
        static Shader::ShaderManager::DefineMap getShadowsDisabledDefines();

        static const ShadowManager& instance();

        explicit ShadowManager(const osg::ref_ptr<osg::Group>& sceneRoot, const osg::ref_ptr<osg::Group>& rootNode,
            std::uint32_t outdoorShadowCastingMask, std::uint32_t indoorShadowCastingMask, std::uint32_t worldMask,
            const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager);
        ~ShadowManager();
        ShadowManager(const ShadowManager&) = delete;
        ShadowManager& operator=(const ShadowManager&) = delete;

        void setupShadowSettings(const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager);
        void setOutdoorShadowCastingMask(std::uint32_t mask);
        void setIndoorShadowCastingMask(std::uint32_t mask);

        void setupShaders(Shader::ShaderManager& shaderManager) const;

        void disableShadowsForStateSet(osg::StateSet& stateset) const;

        Shader::ShaderManager::DefineMap getShadowDefines(const Settings::ShadowsCategory& settings) const;

        void enableIndoorMode(const Settings::ShadowsCategory& settings);

        void enableOutdoorMode();

    protected:
        static ShadowManager* sInstance;

        bool mEnableShadows{};

        osg::ref_ptr<osgShadow::ShadowedScene> mShadowedScene;
        osg::ref_ptr<osgShadow::ShadowSettings> mShadowSettings;
        osg::ref_ptr<MWShadowTechnique> mShadowTechnique;

        std::uint32_t mOutdoorShadowCastingMask;
        std::uint32_t mIndoorShadowCastingMask;
    };
}
