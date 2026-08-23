#ifndef OPENMW_MWRENDER_WEATHERRESULT_H
#define OPENMW_MWRENDER_WEATHERRESULT_H

#include <string>

#include <osg/Vec4f>

#include <components/esm/refid.hpp>

namespace MWRender
{
    /// What the weather system worked out this moment is, for whatever draws the sky.
    ///
    /// **Here rather than beside the dome that reads it.** These are the weather's own numbers —
    /// colours, speeds, the textures it names — computed by `MWWorld::WeatherManager` and handed
    /// down. They lived in `gl/skyutil.hpp` next to their one consumer, which made every file that
    /// wanted to describe the weather include a header full of `osgParticle` shooters and state-set
    /// updaters, and made the world's weather system name a renderer.

    struct WeatherResult
    {
        std::string mCloudTexture;
        std::string mNextCloudTexture;
        float mCloudBlendFactor;

        osg::Vec4f mFogColor;

        osg::Vec4f mAmbientColor;

        osg::Vec4f mSkyColor;

        // sun light color
        osg::Vec4f mSunColor;

        // alpha is the sun transparency
        osg::Vec4f mSunDiscColor;

        float mFogDepth;

        float mDLFogFactor;
        float mDLFogOffset;

        float mWindSpeed;
        float mBaseWindSpeed;
        float mCurrentWindSpeed;
        float mNextWindSpeed;

        float mCloudSpeed;

        float mGlareView;

        bool mNight; // use night skybox
        float mNightFade; // fading factor for night skybox

        bool mIsStorm;

        ESM::RefId mAmbientLoopSoundID;
        ESM::RefId mRainLoopSoundID;
        float mAmbientSoundVolume;

        std::string mParticleEffect;
        std::string mRainEffect;
        float mPrecipitationAlpha;

        float mRainDiameter;
        float mRainMinHeight;
        float mRainMaxHeight;
        float mRainSpeed;
        float mRainEntranceSpeed;
        int mRainMaxRaindrops;

        osg::Vec3f mStormDirection;
        osg::Vec3f mNextStormDirection;
    };

    struct MoonState
    {
        enum class Phase
        {
            Full,
            WaningGibbous,
            ThirdQuarter,
            WaningCrescent,
            New,
            WaxingCrescent,
            FirstQuarter,
            WaxingGibbous,
            Unspecified
        };

        static constexpr unsigned int phaseToInt(Phase phase)
        {
            switch (phase)
            {
                case Phase::New:
                    return 0;
                case Phase::WaxingCrescent:
                case Phase::WaningCrescent:
                    return 1;
                case Phase::FirstQuarter:
                case Phase::ThirdQuarter:
                    return 2;
                case Phase::WaxingGibbous:
                case Phase::WaningGibbous:
                    return 3;
                case Phase::Full:
                    return 4;
                case Phase::Unspecified:
                    return 0;
            }
            return 0;
        }

        float mRotationFromHorizon;
        float mRotationFromNorth;
        Phase mPhase;
        float mShadowBlend;
        float mMoonAlpha;
    };

}

#endif
