#include "fsrframe.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstdint>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    const Shaders::FsrConstants& FsrFrame::advance(const Frame& frame)
    {
        const bool reset = frame.mReset || mFirst;
        mFirst = false;
        mParity = 1 - mParity;

        Shaders::FsrConstants& at = mConstants;
        const Shaders::ivec2 render(static_cast<int>(frame.mRender.width), static_cast<int>(frame.mRender.height));
        const Shaders::ivec2 output(static_cast<int>(frame.mOutput.width), static_cast<int>(frame.mOutput.height));

        at.mPreviousJitter = at.mJitter;
        at.mJitter = -frame.mCamera.mJitter;

        at.mPreviousRenderSize = at.mRenderSize;
        at.mRenderSize = render;
        at.mMaxRenderSize = render;

        at.mPreviousUpscaleSize = at.mUpscaleSize;
        at.mUpscaleSize = output;
        at.mMaxUpscaleSize = output;

        // The horizontal half angle's tangent, which FSR works out from the vertical one and the
        // aspect and the camera states outright.
        const float tanHalfWidth = frame.mCamera.mRight.length();
        const float tanHalfHeight = frame.mCamera.mUp.length();
        at.mTanHalfFov = tanHalfWidth;
        at.mViewSpaceToMetres = 1.0f / Shaders::UNITS_PER_METRE;

        // `setupDeviceDepthToViewSpaceDepthParams`, reversed and infinite: the SDK's `c` is
        // `0 + FLT_EPSILON` and its `e` the near plane, and the last two are one over its
        // projection's `a` and `b`, the half extents' tangents.
        at.mDeviceToViewDepth = Shaders::vec4(-(0.0f + FLT_EPSILON), sNear, tanHalfWidth, tanHalfHeight);

        at.mDownscaleFactor = Shaders::vec2(static_cast<float>(render.x()) / static_cast<float>(output.x()),
            static_cast<float>(render.y()) / static_cast<float>(output.y()));

        // A pre-exposure of one, the renderer's frame being linear radiance before any exposure.
        at.mDeltaPreExposure = 1.0f;

        // The vectors arrive in render pixels, and the target is the render extent's UV.
        at.mMotionVectorScale
            = Shaders::vec2(1.0f / static_cast<float>(render.x()), 1.0f / static_cast<float>(render.y()));
        at.mMotionVectorJitterCancellation = Shaders::vec2(0.0f, 0.0f);

        // A change of the phase count walks the count a step a frame, as the SDK's lock logic wants.
        const float phases = static_cast<float>(jitterPhasesFor(frame.mRender.width, frame.mOutput.width));
        if (reset || at.mJitterPhases == 0.0f)
            at.mJitterPhases = phases;
        else if (phases > at.mJitterPhases)
            at.mJitterPhases += 1.0f;
        else if (phases < at.mJitterPhases)
            at.mJitterPhases -= 1.0f;

        at.mDeltaTime = std::clamp(frame.mDeltaMs / 1000.0f, 0.0f, 1.0f);
        at.mFrameIndex = reset ? 0.0f : at.mFrameIndex + 1.0f;

        at.mVelocityFactor = 1.0f;
        at.mReactivenessScale = 1.0f;
        at.mShadingChangeScale = 1.0f;
        at.mAccumulationAddedPerFrame = 1.0f / 3.0f;
        at.mMinDisocclusionAccumulation = -1.0f / 3.0f;

        return at;
    }

    FsrFrame::Pyramid FsrFrame::pyramidFor(const VkExtent2D render)
    {
        // `ffxSpdSetup` over the rectangle from the corner: a workgroup a 64-pixel tile, and as many
        // levels as the longer side halves in whole, twelve at most — the SDK's `floor(log2(side))`,
        // which in integers is the side's bit width less one.
        constexpr std::uint32_t tile = 64;
        const std::uint32_t groupsX = groupsFor(render.width, tile);
        const std::uint32_t groupsY = groupsFor(render.height, tile);
        const auto mips = std::min(
            static_cast<std::uint32_t>(std::bit_width(std::max(render.width, render.height))) - 1, std::uint32_t{ 12 });

        return Pyramid{
            .mConstants = Shaders::FsrPyramidConstants{
                .mMips = mips,
                .mWorkGroups = groupsX * groupsY,
                .mWorkGroupOffset = Shaders::uvec2(0, 0),
                .mRenderSize = Shaders::uvec2(render.width, render.height),
            },
            .mGroupsX = groupsX,
            .mGroupsY = groupsY,
        };
    }
}
