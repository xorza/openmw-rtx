#include "ripplepass.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <osg/Vec2f>
#include <osg/Vec2i>

#include <components/rtx/environment/wavecascade.hpp>
#include <components/rtx/shaders/wave.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/ripple.h>

namespace Rtx
{
    namespace
    {
        /// The field a step reads, the one it writes, and the impulses it presses.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::RIPPLE_STEP_BINDINGS> sStepBindings{
            computeBinding(Shaders::RIPPLE_STEP_BIND_BEFORE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RIPPLE_STEP_BIND_AFTER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RIPPLE_STEP_BIND_IMPULSES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// The field in, and the two tiles out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::RIPPLE_COMPOSE_BINDINGS> sComposeBindings
            = computeBindings<Shaders::RIPPLE_COMPOSE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        constexpr VkFormat sFieldFormat = toVulkanFormat(RIPPLE_FIELD_FORMAT);

        constexpr std::uint32_t sGrid = Shaders::RIPPLE_GRID;

        constexpr Groups sGroups = Groups::covering(sGrid, sGrid, Shaders::RIPPLE_WORKGROUP);
    }

    RipplePass::RipplePass(const Device& device)
        : mDevice(device)
        , mStepPipeline(device, sStepBindings, {}, "ripplestep.comp.spv", "ripple step")
        , mComposePipeline(device, sComposeBindings, {}, "ripplecompose.comp.spv", "ripple compose")
        , mSampler(makeBorderSampler(device, "ripples"))
        , mImpulses([&](const FrameSlot) {
            return Buffer::hostWritten(device, Shaders::RIPPLE_IMPULSES_MOST * sizeof(Shaders::GpuRippleImpulse),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "ripple impulses");
        })
    {
        constexpr VkImageUsageFlags fieldUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        constexpr VkImageUsageFlags tileUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        const std::uint32_t levels = levelsFor(sGrid);

        mFields[0] = Image(device, sGrid, sGrid, sFieldFormat, fieldUsage, "ripple field 0");
        mFields[1] = Image(device, sGrid, sGrid, sFieldFormat, fieldUsage, "ripple field 1");
        mSurface = Image(device, sGrid, sGrid, toVulkanFormat(WAVE_TILE_FORMAT), tileUsage, "ripple surface", levels);
        mCurvature
            = Image(device, sGrid, sGrid, toVulkanFormat(WAVE_TILE_FORMAT), tileUsage, "ripple curvature", levels);

        mImpulseScratch.reserve(Shaders::RIPPLE_IMPULSES_MOST);
        mPending.reserve(Shaders::RIPPLE_IMPULSES_MOST);

        // Still water in every tile from the first frame, in the layout the trace samples them in:
        // a frame with no water records nothing here and binds them anyway.
        mDevice.getPool().submitAndWait([&](VkCommandBuffer commands) {
            const VkClearColorValue still{ .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };
            for (const Image* image : { &mSurface, &mCurvature })
                image->clear(commands, Use::sUndefined, still, Use::sShaderSample);
            for (const Image& field : mFields)
                field.clear(commands, Use::sUndefined, still, Use::sComputeReadWrite);
        });
    }

    osg::Vec2i RipplePass::windowOf(const osg::Vec2f& eye)
    {
        const auto texel = [](const float along) {
            return static_cast<int>(std::floor(along / Shaders::RIPPLE_TEXEL)) - static_cast<int>(sGrid / 2);
        };

        return osg::Vec2i(texel(eye.x()), texel(eye.y()));
    }

    void RipplePass::standWindow(const osg::Vec2i& window)
    {
        mWindow = window;
        mOrigin = osg::Vec2f(static_cast<float>(window.x()), static_cast<float>(window.y())) * Shaders::RIPPLE_TEXEL;
    }

    void RipplePass::record(const VkCommandBuffer commands, const FrameSlot slot,
        const std::span<const RippleImpulse> impulses, const osg::Vec2f& eye, const double waterSeconds,
        GpuTimer* const timer)
    {
        // A field that was reset, or never started, stands still where the eye is now and owes
        // nothing to where it stood.
        if (mReset)
        {
            standWindow(windowOf(eye));
            mSteppedSeconds = waterSeconds;
            mLastStep = 0.0f;
            mReset = false;

            // The tiles too, because the step below is due only once the clock moves and the trace
            // samples them in between.
            const VkClearColorValue still{ .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };
            for (const Image& field : mFields)
                field.clear(commands, Use::sComputeReadWrite, still, Use::sComputeReadWrite);
            for (const Image* image : { &mSurface, &mCurvature })
                image->clear(commands, Use::sShaderSample, still, Use::sShaderSample);
        }

        // What this frame disturbed, kept until the step that presses it: a frame the water's clock
        // stood still over steps nothing, and its footfalls wait for the step rather than being
        // dropped. Capped at what the buffer holds, oldest first.
        for (const RippleImpulse& impulse : impulses)
            if (mPending.size() < Shaders::RIPPLE_IMPULSES_MOST)
                mPending.push_back(impulse);

        // **Every frame the clock moved, by the time it moved**, where `RipplesSurface::updateState`
        // steps once a sixtieth: at 120 frames a second that left every other frame the whole cost
        // of a step and the frames between none, and under 60 the wake slowed. A clock that stood
        // still or ran backwards steps nothing and moves no window.
        const double elapsed = (waterSeconds - mSteppedSeconds) * static_cast<double>(Shaders::RIPPLE_STEP_RATE);
        if (!(elapsed > 0.0))
            return;

        mSteppedSeconds = waterSeconds;

        // As few steps as keep each under the longest the springs stand, and no more than
        // `RIPPLE_SUBSTEPS_MOST`: a frame slower than those drops the rest of its time.
        const auto steps
            = static_cast<std::uint32_t>(std::min<double>(std::ceil(elapsed / static_cast<double>(getLongestStep())),
                static_cast<double>(Shaders::RIPPLE_SUBSTEPS_MOST)));
        const float step = std::min(static_cast<float>(elapsed / static_cast<double>(steps)), getLongestStep());

        openZone(timer, commands, "ripples");

        // The window follows the eye by whole texels, and the step reads the old field at the
        // offset the window moved by.
        const osg::Vec2i window = windowOf(eye);
        const osg::Vec2i shift = window - mWindow;
        standWindow(window);

        // The impulses in the new window's texels.
        mImpulseScratch.clear();
        for (const RippleImpulse& impulse : mPending)
            mImpulseScratch.push_back(Shaders::GpuRippleImpulse{
                .mAt = (impulse.mAt - mOrigin) / Shaders::RIPPLE_TEXEL,
                .mRadius = impulse.mSize / Shaders::RIPPLE_TEXEL,
                .mStrength = -1.0f,
            });
        mPending.clear();

        Buffer& impulseBuffer = mImpulses.at(slot);
        if (!mImpulseScratch.empty())
            impulseBuffer.write(std::span<const Shaders::GpuRippleImpulse>(mImpulseScratch));

        // The first step reads the old window at its shift and presses the frame's impulses; the
        // steps after it carry on from it.
        for (std::uint32_t at = 0; at < steps; ++at)
        {
            const Image& before = mFields[mLatest];
            const Image& after = mFields[1 - mLatest];
            mLatest = 1 - mLatest;

            DescriptorWrites writes(mStepPipeline);
            writes.image(Shaders::RIPPLE_STEP_BIND_BEFORE, before.describeStorage());
            writes.image(Shaders::RIPPLE_STEP_BIND_AFTER, after.describeStorage());
            writes.buffer(Shaders::RIPPLE_STEP_BIND_IMPULSES, impulseBuffer.describe());

            // A field that has taken no step yet holds two equal heights, so the ratio is moot; one.
            const float last = mLastStep > 0.0f ? mLastStep : step;
            const Shaders::RippleStepConstants stepped{
                .mShift = at == 0 ? shift : osg::Vec2i(),
                .mCount = at == 0 ? static_cast<std::uint32_t>(mImpulseScratch.size()) : 0u,
                .mCarry = step / last * std::pow(1.0f - Shaders::RIPPLE_VELOCITY_DAMPING, step),
                .mScale = 0.5f * step * (step + last),
            };
            mLastStep = step;

            dispatch(commands, mStepPipeline, writes, stepped, sGroups);

            // The step wrote what the compose reads, and what the next step reads back.
            handOver(commands, Use::sBufferComputeWrite, Use::sBufferComputeReadWrite);
        }

        const Image& after = mFields[mLatest];

        // Every level of both tiles is written whole below — the first by the compose, the rest by
        // the chain — so none needs what the last frame left in it. Whatever last touched them,
        // the trace that sampled them or a reset's clear, is behind the head barrier
        // `CommandPool::begin` recorded: a reset's own frame moves no clock and never gets here.
        Barriers opened(commands);
        for (const Image* image : { &mSurface, &mCurvature })
            image->addTransition(opened, Use::sUndefined, Use::sComputeWrite);
        opened.flush();

        DescriptorWrites composes(mComposePipeline);
        composes.image(Shaders::RIPPLE_COMPOSE_BIND_FIELD, after.describeStorage());
        composes.image(Shaders::RIPPLE_COMPOSE_BIND_SURFACE, mSurface.describeStorage());
        composes.image(Shaders::RIPPLE_COMPOSE_BIND_CURVATURE, mCurvature.describeStorage());

        dispatch(commands, mComposePipeline, composes, NoConstants{}, sGroups);

        Image::buildMips(commands, std::array<const Image*, 2>{ &mSurface, &mCurvature });

        closeZone(timer, commands);
    }
}
