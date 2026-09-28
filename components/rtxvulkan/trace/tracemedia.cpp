#include "tracemedia.hpp"

#include <cmath>
#include <cstdint>

#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/scene/devicescene.hpp>

#include "visibilitypass.hpp"

namespace Rtx
{
    TraceMedia::TraceMedia(const Device& device, const std::filesystem::path& shaders)
        : mWaves(device, shaders)
        , mRipples(device, shaders)
        , mFog(device)
        , mNoSprites(Buffer::hostWritten(
              device, 2 * sizeof(std::uint32_t), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, "no sprites"))
    {
        // `SPRITE_LIST_UNBINNED` and a count of nought are both nought.
        mNoSprites.clear();
    }

    TraceSubject TraceMedia::describe(const DeviceScene& held, const Shaders::VisibilityConstants& camera,
        const Buffer& counts, const Buffer& sunGlare, const FrameSlot traceSlot) const
    {
        return TraceSubject{
            .mScene = &held,
            .mMedia = this,
            .mTraceSlot = traceSlot,
            .mCounts = &counts,
            .mDrawsSprites = (camera.mRayMask & Shaders::MASK_PARTICLE) != 0,
            .mSunGlare = &sunGlare,
            .mSea = held.getCounts().mWater > 0 || !std::isinf(camera.mWaterLevel),
        };
    }

    void TraceMedia::keepRipples(const SceneDesc& scene)
    {
        mImpulses.assign(scene.ripples().begin(), scene.ripples().end());
    }

    void TraceMedia::stepRipples(const VkCommandBuffer commands, const FrameSlot slot, const osg::Vec2f& eye,
        const double waterSeconds, GpuTimer* const timer)
    {
        mRipples.record(commands, slot, mImpulses, eye, waterSeconds, timer);
    }

    void TraceMedia::placeRipples(Shaders::VisibilityConstants& sampled) const
    {
        sampled.mRippleOrigin = mRipples.getOrigin();
        sampled.mRippleExtent = RipplePass::getExtent();
    }
}
