#include "framedescriber.hpp"

#include <cassert>
#include <utility>

#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/framesampling.hpp>

namespace Rtx
{
    FrameTraced FrameDescriber::describe(const FrameRequest& request, WorldOptions& described)
    {
        assert(request.mSky != nullptr && "a frame described with no sky to read its sheets from");

        FrameTraced traced{ .mConstants = constantsFor(request.mView), .mCarried = {} };
        Shaders::VisibilityConstants& constants = traced.mConstants;
        constants.mRayMask = request.mRayMask;
        constants.mNoLamps = request.mLamps ? 0u : 1u;
        constants.mFrame = request.mSampleFrame;

        describeWorld(request.mWorld, *request.mSky, mDrift, constants, described);
        traced.mCarried = mDrift.get();
        return traced;
    }

    Shaders::VisibilityConstants FrameDescriber::describePicture(const Viewpoint& view, const GuiTraceOptions& options)
    {
        Shaders::VisibilityConstants camera = constantsFor(view);
        camera.mSun = Shaders::sunSource(options.mLight.mDirection, options.mLight.mIrradiance);
        camera.mAmbient = options.mLight.mAmbient;
        camera.mNoSkyShadows = 1;
        camera.mTransparentBackground = options.mClear[3] < 1.0f ? 1u : 0u;
        camera.mRayMask = options.mRayMask;
        camera.mNoLamps = options.mLamps ? 0u : 1u;
        return camera;
    }

    FramePast FrameDescriber::takePast(const HistoryLoss loss)
    {
        FramePast past = std::exchange(mPast, FramePast{});
        past |= FramePast::of(loss);
        assert((past.mReprojectionLost || mPrevious.has_value()) && "a past with no camera to reproject");
        return past;
    }

    const Shaders::VisibilityConstants* FrameDescriber::reprojectingFrom(const FramePast& past) const
    {
        return past.mReprojectionLost ? nullptr : &*mPrevious;
    }

    void FrameDescriber::traced(const Shaders::VisibilityConstants& stated, const Shaders::VisibilityConstants& sampled)
    {
        mPrevious = previousOf(stated, sampled);
    }

    std::optional<std::uint32_t> FrameDescriber::getTracedFrame() const
    {
        return mPrevious.has_value() ? std::optional(mPrevious->mFrame) : std::nullopt;
    }
}
