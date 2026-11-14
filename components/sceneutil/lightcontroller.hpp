#ifndef OPENMW_COMPONENTS_SCENEUTIL_LIGHTCONTROLLER_H
#define OPENMW_COMPONENTS_SCENEUTIL_LIGHTCONTROLLER_H

#include <components/sceneutil/nodecallback.hpp>
#include <osg/Node>
#include <osg/Vec4f>

namespace SceneUtil
{

    /// @brief Controller class to handle a pulsing and/or flickering light
    // A node and not the light source it drives, which this header cannot see whole: the light manager includes it.
    class LightController : public SceneUtil::NodeCallback<LightController, osg::Node*>
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

        LightController();

        void setType(LightType type);

        void setDiffuse(const osg::Vec4f& color);
        void setSpecular(const osg::Vec4f& color);

        void operator()(osg::Node* node, osg::NodeVisitor* nv);

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
