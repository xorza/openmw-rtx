#include "ripplepass.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <osg/Vec2f>
#include <osg/Vec2i>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/wave.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
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
            computeBinding(Shaders::RIPPLE_STEP_BIND_MOVING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// The field in, and the two tiles out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::RIPPLE_COMPOSE_BINDINGS> sComposeBindings
            = computeBindings<Shaders::RIPPLE_COMPOSE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        constexpr VkFormat sFieldFormat = toVulkanFormat(RIPPLE_FIELD_FORMAT);

        constexpr std::uint32_t sGrid = Shaders::RIPPLE_GRID;

        constexpr Groups sGroups = Groups::covering(sGrid, sGrid, Shaders::RIPPLE_WORKGROUP);

        /// The whole sixtieths the water's clock stands past its start at `seconds`. **A millionth of a sixtieth
        /// over**, so a clock that reaches a sixtieth as a sum of frame times, a rounding short of it, takes that
        /// step on that frame and not the next: a double's rounding of a clock a month on is under a tenth of it,
        /// and a frame is a million times over it.
        std::int64_t ticksOf(const double seconds)
        {
            return static_cast<std::int64_t>(
                std::floor(seconds * static_cast<double>(Shaders::RIPPLE_STEP_RATE) + 1.0e-6));
        }
    }

    RipplePass::RipplePass(const Device& device)
        : mDevice(device)
        , mStepPipeline(device, sStepBindings, {}, "ripplestep.comp.spv", "ripple step")
        , mComposePipeline(device, sComposeBindings, {}, "ripplecompose.comp.spv", "ripple compose")
        , mMips(device)
        , mSampler(makeBorderSampler(device, "ripples"))
        , mImpulses([&](const FrameSlot) {
            return Buffer::hostWritten(device, Shaders::RIPPLE_IMPULSES_MOST * sizeof(Shaders::GpuRippleImpulse),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "ripple impulses");
        })
        , mMoving([&](const FrameSlot) {
            return Buffer::readBack(device, sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "ripple moving");
        })
        , mReportedAt([](const FrameSlot) { return std::optional<std::uint64_t>(); })
    {
        constexpr VkImageUsageFlags fieldUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        constexpr VkImageUsageFlags tileUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        const ImageDescription fieldImage{
            .mWidth = sGrid, .mHeight = sGrid, .mFormat = sFieldFormat, .mUsage = fieldUsage
        };
        const ImageDescription tileImage{ .mWidth = sGrid,
            .mHeight = sGrid,
            .mFormat = toVulkanFormat(WAVE_TILE_FORMAT),
            .mUsage = tileUsage,
            .mMipLevels = levelsTo1x1(sGrid, sGrid) };

        mFields[0] = Image(MemoryUse::Essential, device, fieldImage, "ripple field 0");
        mFields[1] = Image(MemoryUse::Essential, device, fieldImage, "ripple field 1");
        mSurface = Image(MemoryUse::Essential, device, tileImage, "ripple surface");
        mCurvature = Image(MemoryUse::Essential, device, tileImage, "ripple curvature");

        mImpulseScratch.reserve(Shaders::RIPPLE_IMPULSES_MOST);
        mCandidates.reserve(Shaders::RIPPLE_IMPULSES_MOST);

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
            return static_cast<std::int32_t>(std::floor(along / Shaders::RIPPLE_TEXEL))
                - static_cast<std::int32_t>(sGrid / 2);
        };

        return osg::Vec2i(texel(eye.x()), texel(eye.y()));
    }

    osg::Vec2f RipplePass::originOf(const osg::Vec2i& window)
    {
        return osg::Vec2f(static_cast<float>(window.x()), static_cast<float>(window.y())) * Shaders::RIPPLE_TEXEL;
    }

    void RipplePass::standWindow(const osg::Vec2i& window)
    {
        mWindow = window;
        mOrigin = originOf(window);
    }

    void RipplePass::record(const VkCommandBuffer commands, Uploads& uploads, const FrameSlot slot,
        const std::span<const RippleImpulse> impulses, const osg::Vec2f& eye, const double waterSeconds,
        GpuTimer* const timer)
    {
        ++mFrames;

        // What the slot's last frame reported, once its submit is done: nought there, with nothing
        // pressed since, is a field nought now — a step of nought is nought. Asked of the timeline
        // and not taken from the ring's order, because a frame recorded into the same submit as the
        // one that wrote the word has not run it yet.
        const Buffer& moving = mMoving.at(slot);
        if (const std::optional<std::uint64_t> reported = std::exchange(mReportedAt.at(slot), std::nullopt);
            reported.has_value() && mDevice.getTimeline().hasFinished(moving.getNamedUntil())
            && *static_cast<const std::uint32_t*>(moving.map()) == 0 && mPressedAt <= *reported)
            mStill = true;

        // A field that was reset, or never started, stands still where the eye is now and owes
        // nothing to where it stood.
        if (mReset)
        {
            standWindow(windowOf(eye));
            mSeenSeconds = waterSeconds;
            mCandidates.clear();
            mOrder = 0;
            mReset = false;

            // The tiles too, because the step below is due only once the clock moves and the trace
            // samples them in between. Nought is what the compose writes of a field of nought, so
            // the field is still from here.
            const VkClearColorValue still{ .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };
            for (const Image& field : mFields)
                field.clear(commands, Use::sComputeReadWrite, still, Use::sComputeReadWrite);
            for (const Image* image : { &mSurface, &mCurvature })
                image->clear(commands, Use::sShaderSample, still, Use::sShaderSample);
            mStill = true;
        }

        // A clock that stood still or ran backwards steps nothing, moves no window and presses
        // nothing.
        const double elapsed = (waterSeconds - mSeenSeconds) * static_cast<double>(Shaders::RIPPLE_STEP_RATE);
        if (!(elapsed > 0.0))
            return;

        // A step for every sixtieth the clock crossed, and no more than `RIPPLE_FRAME_STEPS_MOST`: a
        // frame slower than those drops the rest of its time, its press included.
        const std::int64_t crossed = ticksOf(waterSeconds) - ticksOf(mSeenSeconds);
        assert(crossed >= 0);
        const auto steps
            = static_cast<std::uint32_t>(std::min<std::int64_t>(crossed, Shaders::RIPPLE_FRAME_STEPS_MOST));
        mSeenSeconds = waterSeconds;
        const auto press = static_cast<float>(std::min(elapsed, static_cast<double>(Shaders::RIPPLE_FRAME_STEPS_MOST)));

        // The window this frame's eye would stand, which a kept impulse's ring must reach: the step
        // that presses it may stand the window a few texels on, where a ring that misses it presses
        // no texel.
        const osg::Vec2i window = windowOf(eye);
        const osg::Vec2f origin = originOf(window);
        const auto grid = static_cast<float>(sGrid);
        const osg::Vec2f eyeAt = (eye - origin) / Shaders::RIPPLE_TEXEL;

        // **Past what the buffer holds, the nearest the eye, and of two as near the one the game gave
        // first**, kept in a heap of the buffer's size whose top is the one to give up: a crowded
        // frame grows nothing, where every candidate kept and trimmed after grew the list past its
        // reserve. Kept across the frames that take no step, and pressed in the game's order.
        const auto nearer = [](const Candidate& left, const Candidate& right) {
            return left.mDistance != right.mDistance ? left.mDistance < right.mDistance : left.mOrder < right.mOrder;
        };
        for (const RippleImpulse& impulse : impulses)
        {
            // Asked as whether it reaches, so an impulse at no place, a NaN the world handed on,
            // reaches nothing: pressed, it would be NaN in every texel of the window.
            const osg::Vec2f at = (impulse.mAt - origin) / Shaders::RIPPLE_TEXEL;
            const float reach = 2.0f * impulse.mSize / Shaders::RIPPLE_TEXEL;
            const bool reaches = at.x() + reach > 0.5f && at.x() - reach < grid - 0.5f && at.y() + reach > 0.5f
                && at.y() - reach < grid - 0.5f;
            if (!reaches)
                continue;

            const Candidate candidate{
                .mDistance = (at - eyeAt).length2(), .mOrder = mOrder++, .mImpulse = impulse, .mPress = press
            };
            if (mCandidates.size() < Shaders::RIPPLE_IMPULSES_MOST)
            {
                mCandidates.push_back(candidate);
                std::ranges::push_heap(mCandidates, nearer);
            }
            else if (nearer(candidate, mCandidates.front()))
            {
                std::ranges::pop_heap(mCandidates, nearer);
                mCandidates.back() = candidate;
                std::ranges::push_heap(mCandidates, nearer);
            }
        }
        assert(mCandidates.size() <= Shaders::RIPPLE_IMPULSES_MOST);

        if (steps == 0)
            return;

        // The window follows the eye by whole texels, and the step reads the old field at the
        // offset the window moved by.
        const osg::Vec2i shift = window - mWindow;
        standWindow(window);

        std::ranges::sort(mCandidates, {}, &Candidate::mOrder);
        mImpulseScratch.clear();
        for (const Candidate& candidate : mCandidates)
            mImpulseScratch.push_back(
                Shaders::GpuRippleImpulse{ .mAt = (candidate.mImpulse.mAt - mOrigin) / Shaders::RIPPLE_TEXEL,
                    .mRadius = candidate.mImpulse.mSize / Shaders::RIPPLE_TEXEL,
                    .mPress = candidate.mPress });
        mCandidates.clear();
        mOrder = 0;

        // A still field nothing presses: nothing to step and nothing to compose.
        if (mStill && mImpulseScratch.empty())
            return;
        mStill = false;
        if (!mImpulseScratch.empty())
            mPressedAt = mFrames;

        const GpuZone timed(timer, commands, FrameZone::Ripples);

        Buffer& impulseBuffer = mImpulses.at(slot);
        if (!mImpulseScratch.empty())
        {
            uploads.write(impulseBuffer, 0, std::span<const Shaders::GpuRippleImpulse>(mImpulseScratch));
            uploads.order();
        }

        // Cleared on the queue, behind whatever step last wrote it: the host's read of it came first,
        // and a frame recorded into the same submit as the last stands after that one's step.
        moving.transition(commands, Use::sBufferComputeReadWrite, Use::sBufferClearWrite);
        moving.clear(commands);
        moving.transition(commands, Use::sBufferClearWrite, Use::sBufferComputeReadWrite);

        // The first step reads the old window at its shift and presses the impulses; the steps
        // after it carry on from it, and the last says whether anything still moves.
        for (std::uint32_t at = 0; at < steps; ++at)
        {
            const Image& before = mFields[mLatest];
            const Image& after = mFields[1 - mLatest];
            mLatest = 1 - mLatest;

            DescriptorWrites writes(mStepPipeline);
            writes.image(Shaders::RIPPLE_STEP_BIND_BEFORE, before.describeStorage());
            writes.image(Shaders::RIPPLE_STEP_BIND_AFTER, after.describeStorage());
            writes.buffer(Shaders::RIPPLE_STEP_BIND_IMPULSES, impulseBuffer.describe());
            writes.buffer(Shaders::RIPPLE_STEP_BIND_MOVING, moving.describe());

            const Shaders::RippleStepConstants stepped{
                .mShift = at == 0 ? shift : osg::Vec2i(),
                .mCount = at == 0 ? static_cast<std::uint32_t>(mImpulseScratch.size()) : 0u,
                .mReport = at + 1 == steps ? 1u : 0u,
            };

            dispatch(commands, mStepPipeline, writes, stepped, sGroups);

            // The step wrote what the compose reads, and what the next step reads back.
            handOver(commands, Use::sBufferComputeWrite, Use::sBufferComputeReadWrite);
        }
        moving.orderForHostRead(commands);
        mReportedAt.at(slot) = mFrames;

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

        mMips.record(commands, std::array<const Image*, 2>{ &mSurface, &mCurvature });
    }
}
