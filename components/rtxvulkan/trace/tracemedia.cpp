#include "tracemedia.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/shaders/shared/bindings.h>

#include "tracesubject.hpp"

namespace Rtx
{
    TraceMedia::TraceMedia(const Device& device, const FogNoise& fog)
        : mWaves(device)
        , mRipples(device)
        , mFog(device, fog)
    {
    }

    TraceSubject TraceMedia::describe(const DeviceScene& held, const Shaders::VisibilityConstants& camera,
        const Buffer& counts, const Buffer& sunGlare) const
    {
        return TraceSubject{
            .mScene = &held,
            .mMedia = this,
            .mCounts = &counts,
            .mDrawsSprites = (camera.mRayMask & Shaders::MASK_PARTICLE) != 0,
            .mSunGlare = &sunGlare,
            .mSea = held.getCounts().mWater > 0 || !std::isinf(camera.mWaterLevel),
            .mMapped = held.getCounts().mMapped > 0,
            .mLamps = held.getBuffers().getLightGrid().getLightCount() > 0,
        };
    }

    void TraceMedia::bindInto(DescriptorWrites& writes) const
    {
        std::array<VkDescriptorImageInfo, Shaders::WAVE_CASCADES> surfaces{};
        std::array<VkDescriptorImageInfo, Shaders::WAVE_CASCADES> curvatures{};
        for (std::size_t cascade = 0; cascade < Shaders::WAVE_CASCADES; ++cascade)
        {
            const VkSampler sampler = mWaves.getSampler();
            surfaces[cascade] = mWaves.getSurface(cascade).describeSampled(sampler);
            curvatures[cascade] = mWaves.getCurvature(cascade).describeSampled(sampler);
        }
        writes.images(Shaders::BIND_WAVE_SURFACE, surfaces);
        writes.images(Shaders::BIND_WAVE_CURVATURE, curvatures);

        writes.image(Shaders::BIND_FOG_FIELD,
            mFog.getField().describeSampled(mFog.getSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

        writes.image(Shaders::BIND_RIPPLE_SURFACE, mRipples.getSurface().describeSampled(mRipples.getSampler()));
        writes.image(Shaders::BIND_RIPPLE_CURVATURE, mRipples.getCurvature().describeSampled(mRipples.getSampler()));
    }

    void TraceMedia::describeFrame(Shaders::VisibilityConstants& described) const
    {
        for (std::size_t cascade = 0; cascade < Shaders::WAVE_CASCADES; ++cascade)
        {
            described.mWaveExtent[cascade] = mWaves.getExtent(cascade);
            described.mWaveTexel[cascade] = mWaves.getTexel(cascade);
        }

        described.mWaveSlope = mWaves.getSlope();

        const WaveCurvature& curvature = mWaves.getMoments();
        described.mWaveCurvature = curvature.mWhole;
        std::copy(curvature.mResolved.begin(), curvature.mResolved.end(), std::begin(described.mWaveResolved));
    }

    void TraceMedia::keepRipples(const SceneDesc& scene)
    {
        mImpulses.assign(scene.ripples().begin(), scene.ripples().end());
    }

    void TraceMedia::stepRipples(const VkCommandBuffer commands, const FrameSlot slot, const osg::Vec2f& eye,
        const double waterSeconds, GpuTimer* const timer)
    {
        mRipples.record(commands, slot, mImpulses, eye, waterSeconds, timer);

        // Spent: what a placement kept is pressed once. A second trace of the same placement would
        // otherwise hand the pass the same footfalls again.
        mImpulses.clear();
    }

    void TraceMedia::synthesiseSea(const VkCommandBuffer commands, const osg::Vec2f& seconds, GpuTimer* const timer)
    {
        if (!mWaves.holds(seconds))
            mWaves.record(commands, seconds, timer);
    }

    void TraceMedia::placeRipples(Shaders::VisibilityConstants& sampled) const
    {
        sampled.mRippleOrigin = mRipples.getOrigin();
        sampled.mRippleExtent = RipplePass::getExtent();
    }
}
