#include "framesampling.hpp"

#include <cassert>
#include <cstdint>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/scene.h>

#include "camera.hpp"
#include "frameoptions.hpp"

namespace Rtx
{
    namespace
    {
        /// How much wider the arms' image plane is than the eye's, per axis —
        /// `VisibilityConstants::mArmsSpread`.
        osg::Vec2f armsSpreadOf(const Shaders::VisibilityConstants& frame)
        {
            return osg::Vec2f(frame.mEyes.mArms.mBasis.mRight.length() / frame.mEyes.mWorld.mBasis.mRight.length(),
                frame.mEyes.mArms.mBasis.mUp.length() / frame.mEyes.mWorld.mBasis.mUp.length());
        }
    }

    Shaders::VisibilityConstants sampleFrame(const Shaders::VisibilityConstants& stated, const FrameOptions& options,
        const RenderProfile& profile, const Reconstruction& reconstruction, const InstanceCounts& counts,
        const Shaders::VisibilityConstants* previous)
    {
        assert(!(reconstruction.mJitter && options.mJitter.has_value())
            && "a frame asked for a fixed offset where the reconstruction walks its own sequence");

        Shaders::VisibilityConstants sampled = stated;

        // Where in the pixel this frame samples. Here rather than by the caller because the
        // sequence belongs to the frame index, which the renderer walks — and cycles, where an
        // upscaler's history is written against a period of phases.
        const std::uint32_t phase
            = reconstruction.mJitterPhases > 0 ? stated.mFrame % reconstruction.mJitterPhases : stated.mFrame;
        sampled.mEyes.mWorld.mJitter
            = reconstruction.mJitter ? haltonJitter(phase) : options.mJitter.value_or(osg::Vec2f());

        // The five consequences of the reconstruction the trace reads for itself: where its draws
        // come from, how far the shown pixel narrows every texture level, whether the eye may draw a
        // soft edge's texels by their alpha, under which share a source is never drawn for the
        // shadow bit, and how many lamps a composing point draws. A picture's `Reconstruction{}`
        // says the tile, nought, no and the two defaults.
        sampled.mNoise = reconstruction.mSampling.mNoise == NoiseSource::WhiteHash ? Shaders::NOISE_WHITE_HASH
                                                                                   : Shaders::NOISE_BLUE_TILE;
        sampled.mLevelBias = reconstruction.mLevelBias;
        sampled.mSoftEdgeDither = reconstruction.mAveraged ? 1u : 0u;
        sampled.mShadowFloor = reconstruction.mSampling.mShadowFloor;
        sampled.mLampCandidates = reconstruction.mSampling.mLampCandidates;

        // The sampler takes the setting as it is: the settings clamp it to sixteen, and a device
        // with `samplerAnisotropy`, which the requirements ask for, takes at least sixteen.
        assert(profile.mAnisotropy >= 1 && "an anisotropy of nought, which no sampler takes");
        sampled.mAnisotropy = static_cast<float>(profile.mAnisotropy);

        sampled.mDelight = options.mDelight.value_or(profile.mDelight);
        sampled.mShow = static_cast<std::uint32_t>(options.mShow.value_or(profile.mShow));
        sampled.mLitEnvironmentMaps = options.mLitEnvironmentMaps.value_or(profile.mLitEnvironmentMaps) ? 1u : 0u;

        // The arms' eye samples where the world's does, or the two halves of one frame would be
        // reconstructed from two grids.
        sampled.mEyes.mArms.mJitter = sampled.mEyes.mWorld.mJitter;
        sampled.mArmsSpread = armsSpreadOf(stated);
        sampled.mUnitRight = stated.mEyes.mWorld.mBasis.mRight / stated.mEyes.mWorld.mBasis.mRight.length();
        sampled.mUnitUp = stated.mEyes.mWorld.mBasis.mUp / stated.mEyes.mWorld.mBasis.mUp.length();

        // The scene's answer and the camera's both: a map draws no arms.
        sampled.mArmsInFrame = counts.mFirstPerson > 0 && (stated.mRayMask & Shaders::MASK_FIRST_PERSON) != 0 ? 1 : 0;

        // The one subtraction of two world points, and it happens here. Two camera positions a
        // step apart subtract exactly in a float; the same difference taken on the device, between
        // coordinates six figures long, would be rounding. Nothing moved where no frame came before.
        sampled.mPreviousJitter = sampled.mEyes.mWorld.mJitter;
        if (previous != nullptr)
        {
            sampled.mCameraMotion = stated.mOrigin - previous->mOrigin;
            sampled.mPrevious = previous->mEyes.mWorld.mBasis;
            sampled.mPreviousJitter = previous->mEyes.mWorld.mJitter;
        }

        return sampled;
    }
}
