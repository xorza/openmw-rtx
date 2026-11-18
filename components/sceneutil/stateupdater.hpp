#pragma once

#include <cstdint>

#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include "statesetupdater.hpp"

namespace osg
{
    class StateSet;
}

namespace Resource
{
    class SceneManager;
}

namespace SceneUtil
{
    class RTTNode;

    class PerViewUniformStateUpdater final : public StateSetUpdater
    {
    public:
        explicit PerViewUniformStateUpdater(Resource::SceneManager* sceneManager, std::int32_t opaqueTextureUnit = -1,
            std::int32_t opaqueColorTextureUnit = -1);

        void setDefaults(osg::StateSet* stateset) override;

        void apply(osg::StateSet* stateset, osg::NodeVisitor* nv) override;

        void applyLeft(osg::StateSet* stateset, osgUtil::CullVisitor* nv) override;

        void applyRight(osg::StateSet* stateset, osgUtil::CullVisitor* nv) override;

        void setProjectionMatrix(const osg::Matrixf& projectionMatrix);

        const osg::Matrixf& getProjectionMatrix() const;

        void enableSkyRTT(std::int32_t skyTextureUnit, RTTNode* skyRTT);

    private:
        osg::Matrixf getEyeProjectionMatrix(std::int32_t view);

        osg::Matrixf mProjectionMatrix;
        std::int32_t mSkyTextureUnit = -1;
        RTTNode* mSkyRTT = nullptr;

        Resource::SceneManager* mSceneManager;
        std::int32_t mOpaqueTextureUnit = -1;
        std::int32_t mOpaqueColorTextureUnit = -1;
    };

    class SharedUniformStateUpdater : public StateSetUpdater
    {
    public:
        explicit SharedUniformStateUpdater(float skyBlendingStartCoef);

        void setDefaults(osg::StateSet* stateset) override;
        void apply(osg::StateSet* stateset, osg::NodeVisitor* nv) override;
        void setNear(float near);

        void setFar(float far);

        void setScreenRes(float width, float height);

        void setWindSpeed(float windSpeed);

        void setPlayerPos(osg::Vec3f playerPos);

    private:
        float mNear = 0.f;
        float mFar = 0.f;
        float mWindSpeed = 0.f;
        float mSkyBlendingStartCoef = 0.f;
        osg::Vec3f mPlayerPos;
        osg::Vec2f mScreenRes;
    };

    class StateUpdater : public StateSetUpdater
    {
    public:
        void setDefaults(osg::StateSet* stateset) override;

        void apply(osg::StateSet* stateset, osg::NodeVisitor*) override;

        void setAmbientColor(const osg::Vec4f& col);

        void setFogColor(const osg::Vec4f& col);

        void setFogStart(float start);

        void setFogEnd(float end);

        void setUnderwaterFogColor(const osg::Vec4f& col);

        void setUnderwaterFogStart(float start);

        void setUnderwaterFogEnd(float end);

        void setWireframe(bool wireframe);
        bool getWireframe() const;

        void setWaterHeight(float waterHeight);

        void setWaterEnabled(bool enabled);

    private:
        osg::Vec4f mAmbientColor;
        osg::Vec4f mFogColor;
        float mFogStart = 0.f;
        float mFogEnd = 0.f;
        osg::Vec4f mUnderwaterFogColor;
        float mUnderwaterFogStart = 0.f;
        float mUnderwaterFogEnd = 0.f;
        bool mWireframe = false;
        float mWaterHeight = 0.f;
        bool mWaterEnabled = false;
    };
}
