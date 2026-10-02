#ifndef OPENMW_COMPONENTS_SCENEUTIL_LIGHTCONTROLLER_H
#define OPENMW_COMPONENTS_SCENEUTIL_LIGHTCONTROLLER_H

#include <components/sceneutil/nodecallback.hpp>
#include <osg/Vec4f>

namespace SceneUtil
{

    class LightSource;

    /// @brief Controller class to handle a pulsing and/or flickering light
    class LightController : public SceneUtil::NodeCallback<LightController, SceneUtil::LightSource*>
    {
    public:
        enum LightType
        {
            LT_Normal,
            LT_Flicker,
            LT_FlickerSlow,
            LT_Pulse,
            LT_PulseSlow
        };

        /// The band an animated light's brightness walks within, as a share of its recorded colour.
        static constexpr float sDimmest = 0.25f;
        static constexpr float sBrightest = 1.f;

        /// How far the brightness moves in one tick, fast and slow, at vanilla's fifteen ticks a
        /// second.
        static constexpr float sFastSpeed = 0.1f;
        static constexpr float sSlowSpeed = 0.05f;
        static constexpr float sTicksPerSecond = 15.f;

        LightController();

        void setType(LightType type);

        void setDiffuse(const osg::Vec4f& color);
        void setSpecular(const osg::Vec4f& color);

        void operator()(SceneUtil::LightSource* node, osg::NodeVisitor* nv);

        LightType getType() const { return mType; }

        /// The diffuse colour before the animation dims it
        const osg::Vec4f& getDiffuse() const { return mDiffuseColor; }

    private:
        LightType mType;
        osg::Vec4f mDiffuseColor;
        osg::Vec4f mSpecularColor;
        float mPhase;
        float mBrightness;
        double mStartTime;
        double mLastTime;
        float mTicksToAdvance;
    };

}

#endif
