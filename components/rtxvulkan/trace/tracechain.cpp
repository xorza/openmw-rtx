#include "tracechain.hpp"

#include <algorithm>
#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/composite.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/scenebuffers.hpp>

#include "tracemedia.hpp"
#include "tracepasses.hpp"
#include "tracerecording.hpp"
#include "visibilitypass.hpp"
#include "wavepass.hpp"

namespace Rtx
{
    TraceChain::TraceChain(const Device& device, const TracePasses& passes)
        : mDevice(device)
        , mPasses(passes)
        , mBins([&](FrameSlot) {
            return SpriteBin{ device, passes.mSpriteShade, passes.mSpriteBin };
        })
        , mHistory(device)
    {
    }

    void TraceChain::resize(const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
    {
        assert(width > 0 && height > 0);

        mWidth = width;
        mHeight = height;

        mChannels = std::make_unique<GBuffer>(mDevice, mPasses.mChannels, mWidth, mHeight, radiance);
        mFogVolume = std::make_unique<FogVolume>(mDevice, mPasses.mFog, mWidth, mHeight);
        mHistory.resize(mWidth, mHeight);
        if (mFilterScratch.isEmpty() || mFilterScratch.getWidth() != mWidth || mFilterScratch.getHeight() != mHeight)
            mFilterScratch = AtrousPass::makeScratch(mDevice, mWidth, mHeight);

        // Dropped rather than resized, because most runs never make one: sixteen bytes a pixel is
        // worth it to the reference mode and nothing to a window. The first averaging trace asks.
        dropSum();
    }

    void TraceChain::grow(const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
    {
        if (holds(width, height))
            return;

        resize(std::max(mWidth, width), std::max(mHeight, height), radiance);
    }

    const Image& TraceChain::recordDenoise(const VkCommandBuffer commands, const Shaders::VisibilityConstants& sampled,
        const bool historyLost, GpuTimer* const timer)
    {
        const Shaders::Camera& camera = sampled.mCamera;
        // The temporal half first: the accumulator hands on the variance of its mean, which is
        // what lets the levels below stop at an edge in the light and not only in the geometry.
        openZone(timer, commands, "accumulate");
        mPasses.mAccumulate.record(commands, mHistory, *mChannels, camera, sampled.mFar, historyLost);
        const Image& blended = mHistory.getBlended();
        closeZone(timer, commands);

        // The cascade reads what the accumulator just wrote, and it reads through the texture unit
        // — so the dependency names the sampled access and not only the storage one. The history
        // the cascade writes for the next frame is ordered by the discard `AccumulatePass::record`
        // made of it, which named a compute write as what would come next.
        blended.transition(commands, Use::sComputeWrite, Use::sComputeReadOrSample);

        openZone(timer, commands, "filter");
        const Image& indirect = mPasses.mFilter.record(
            commands, *mChannels, blended, mHistory.getHistory(), mFilterScratch, camera, sampled.mArms);
        closeZone(timer, commands);

        return indirect;
    }

    void TraceChain::resetHistory()
    {
        mHistory.reset();
        mAirStale = true;
    }

    VkDeviceAddress TraceChain::getSpriteTileList(const VisibilityInputs& inputs) const
    {
        return inputs.mSubject.mDrawsSprites ? mBins.at(inputs.mSubject.mTraceSlot).getTileListAddress()
                                             : inputs.mSubject.mMedia->describeNoSprites();
    }

    TraceResult TraceChain::record(const VkCommandBuffer commands, const TraceRecording& what)
    {
        assert(isBuilt() && "a trace into a chain that has no extent");

        const VisibilityInputs inputs{ .mSubject = what.mSubject, .mChannels = *mChannels, .mFogVolume = *mFogVolume };

        // Made by the first trace that averages, and that trace is the one that fills it: the first
        // write needs no contents and nothing to wait on, and every trace after reads what the last
        // left, which the head barrier `CommandPool::begin` recorded orders and makes visible.
        if (what.mAccumulate > 0 && mSum.isEmpty())
        {
            mSum = Image(
                mDevice, mWidth, mHeight, toVulkanFormat(COMPOSITE_SUM_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT, "sum");
            mSum.transition(commands, Use::sUndefined, Use::sComputeReadWrite);
        }

        // Before the trace and outside its zone, because the sea is a function of the clock and of
        // nothing the camera does — one synthesis serves every ray. None where there is no sea,
        // which is most interiors, and a fifth of a millisecond of device time in each of them.
        if (inputs.mSubject.mSea)
        {
            openZone(what.mTimer, commands, "waves");
            inputs.mSubject.mMedia->getWaves().record(commands, what.mSampled.mWaterTime);
            closeZone(what.mTimer, commands);
        }

        // The sprite tiles are screen space, so they belong to the camera and not to the scene.
        // Binned on the device into this trace's own bin, ahead of the trace that reads it. Not at
        // all for a camera handed a list of its own, which is the one that draws none.
        //
        // **Taken, then the block, then the shelter, then the bin.** The block carries the bin's
        // table by address, which it has once the table is taken; the shelter launch reads the
        // block and zeroes the drops under a roof in that table; and the shade and the bin read
        // what is left. Every launch after reads the same block, and the emitters' rows beside it.
        SpriteBin& bin = mBins.at(inputs.mSubject.mTraceSlot);
        const bool bins = inputs.mSubject.mDrawsSprites;
        const SpriteSource sprites
            = inputs.mSubject.mScene->getBuffers().describeSprites(inputs.mSubject.mScene->getSlot());
        if (bins)
            bin.take(sprites, what.mAsked.mCamera, commands);

        // After the take, which may have grown the table, and once: the frame block and the
        // display's `puffsCoverNothing` read the one list.
        const VkDeviceAddress tileList = getSpriteTileList(inputs);

        // Composed by the trace where nothing filters the bounce: `VisibilityConstants::mComposed`.
        const bool composed = !what.mFilter;

        mPasses.mVisibility.writeFrame(
            commands, inputs, bin, tileList, what.mSampled, what.mPastLost || mAirStale, composed);
        mAirStale = false;

        if (bins)
        {
            mPasses.mVisibility.recordSpriteShelter(commands, inputs, what.mSampled, sprites.mSpriteCount, what.mTimer);
            mPasses.mVisibility.recordSpriteEmitters(commands, inputs, sprites.mEmitterCount, what.mTimer);
            bin.record(commands,
                Binning{
                    .mSource = sprites,
                    .mOrigin = what.mAsked.mOrigin,
                    .mCamera = what.mAsked.mCamera,
                    .mToSun = what.mAsked.mSun.mDirection,
                    .mTimer = what.mTimer,
                });
        }

        mChannels->begin(commands);
        mPasses.mVisibility.record(commands, inputs, what.mSampled, what.mTimer);
        mChannels->handOver(commands);

        // Where the bounce ended up: the filter's last level, or the channel the trace wrote where
        // nothing filtered it.
        const Image* indirect = &mChannels->get(Channel::Indirect);
        if (what.mFilter)
            indirect = &recordDenoise(commands, what.mSampled, what.mPastLost, what.mTimer);

        // **Only where something is left to do**: a filter to put the albedo back in behind, or a sum
        // to add the frame to. Anything else was composed by the trace, into the channel that is
        // the frame, and every pass after it reads the channel as `handOver` left it.
        const Image& frame = mChannels->get(Channel::Direct);
        if (what.mFilter || what.mAccumulate > 0)
        {
            // Written over, where the hand-over left it to be read: nothing has read it since, and
            // this is the dependency that keeps it so.
            frame.transition(commands, Use::sAnyShaderRead, Use::sComputeReadWrite);

            openZone(what.mTimer, commands, "composite");
            mPasses.mComposite.record(commands, *mChannels, *indirect, mSum.isEmpty() ? nullptr : &mSum,
                Shaders::CompositeConstants{
                    .mWidth = what.mSampled.mCamera.mWidth,
                    .mHeight = what.mSampled.mCamera.mHeight,
                    .mAccumulate = what.mAccumulate,
                    .mComposed = composed ? 1u : 0u,
                });
            closeZone(what.mTimer, commands);

            // Whatever comes next reads what the composite just wrote. The frame's scope is the
            // wider of the two — an upscaler, a lens and a curve against a picture's one curve —
            // and covers both.
            frame.transition(commands, Use::sComputeReadWrite, Use::sAnyGeneralRead);
        }

        return TraceResult{ .mInputs = inputs,
            .mColour = frame,
            .mSpriteTileList = tileList,
            .mSpritePresence = bin.getPresenceAddress() };
    }
}
