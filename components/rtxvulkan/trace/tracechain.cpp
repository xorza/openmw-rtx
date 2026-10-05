#include "tracechain.hpp"

#include <algorithm>
#include <cassert>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/scenebuffers.hpp>
#include <components/rtxvulkan/scene/spritesource.hpp>
#include <components/rtxvulkan/shaders/shared/composite.h>
#include <components/rtxvulkan/trace/denoise/denoised.hpp>

#include "tracemedia.hpp"
#include "tracepasses.hpp"
#include "tracerecording.hpp"
#include "visibilitypass.hpp"
#include "wavepass.hpp"

namespace Rtx
{
    TraceChain::TraceChain(const Device& device, const TracePasses& passes, const std::uint32_t bins,
        const RadianceWidth radiance, const bool reuses, const IndirectLight indirect)
        : mDevice(device)
        , mPasses(passes)
        , mRadiance(radiance)
        , mReuses(reuses)
        , mIndirect(indirect)
        , mDenoise(device)
        , mReservoirs(device)
    {
        assert(bins >= 1 && bins <= sFrameSlots && "a sprite bin past the frames in flight");
        mBins.reserve(bins);
        for (std::uint32_t at = 0; at < bins; ++at)
            mBins.emplace_back(device, passes.mSpriteShade, passes.mSpriteBin);
    }

    void TraceChain::resize(const std::uint32_t width, const std::uint32_t height)
    {
        assert(width > 0 && height > 0);

        // **The one owner of "is this a new extent"**: an upscaling mode changed between two that
        // trace at one size asks this again, and fourteen channels and twelve fog images made anew
        // for it would be made for nothing.
        if (isBuilt() && width == mWidth && height == mHeight)
            return;

        mWidth = width;
        mHeight = height;

        mChannels = std::make_unique<GBuffer>(mDevice, mPasses.mChannels, mWidth, mHeight, mRadiance);
        mFogVolume = std::make_unique<FogVolume>(mDevice, mPasses.mFog, mWidth, mHeight);
        const bool traced = mIndirect == IndirectLight::Traced;
        mDenoise.resize(mWidth, mHeight, traced);
        mReservoirs.resize(mWidth, mHeight, mReuses && traced);

        // Dropped rather than resized, because most runs never make one: sixteen bytes a pixel is
        // worth it to the reference mode and nothing to a window. The first averaging trace asks.
        dropSum();
    }

    void TraceChain::setIndirect(const IndirectLight indirect)
    {
        if (indirect == mIndirect)
            return;

        mIndirect = indirect;
        if (!isBuilt())
            return;

        // The denoiser's mean is fresh by the turn's own rule the next frame it runs, and the
        // reservoirs are made anew, which is a reset.
        const bool traced = indirect == IndirectLight::Traced;
        mDenoise.keepBounce(traced);
        mReservoirs.resize(mWidth, mHeight, mReuses && traced);
    }

    void TraceChain::grow(const std::uint32_t width, const std::uint32_t height)
    {
        if (holds(width, height))
            return;

        resize(std::max(mWidth, width), std::max(mHeight, height));
    }

    TraceResult TraceChain::record(const VkCommandBuffer commands, const TraceRecording& what)
    {
        assert(isBuilt() && "a trace into a chain that has no extent");

        // Each denoiser's history is worthless until the next trace that reads it, which is only
        // where the wavelet runs: a frame that filters nothing turns every filter fresh in its
        // turn. The air's is read by every trace, and the basis of nothing the frame carries says
        // so to it.
        if (what.mPastLost)
        {
            mDenoise.reset();
            mReservoirs.reset();
        }

        setIndirect(what.mReconstruction.mIndirect);

        const BounceReuse reuse = what.mReconstruction.mBounceReuse;
        assert((mReuses || reuse == BounceReuse::Off) && "a reuse asked of a chain that keeps no reservoirs");
        const bool history = mReservoirs.turn(reuse != BounceReuse::Off);

        mFogVolume->turn();
        const VisibilityInputs inputs{ .mSubject = what.mSubject,
            .mChannels = *mChannels,
            .mFogVolume = *mFogVolume,
            .mReservoirs = mReservoirs,
            .mBounceReuse = reuse,
            .mBounceHistory = history };

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
        // Binned on the device into this trace's own bin, ahead of the trace that reads it. **For a
        // camera that draws no sprites as well**, with none of them in it: the bin is also where the
        // tiles learn which media and additive surfaces a ray through them can meet, and a camera
        // told nothing would walk both at every pixel.
        //
        // **Taken, then the block, then the shelter, then the bin.** The block carries the bin's
        // table by address, which it has once the table is taken; the shelter launch reads the
        // block and zeroes the drops under a roof in that table; and the shade and the bin read
        // what is left. Every launch after reads the same block, and the emitters' rows beside it.
        assert(inputs.mSubject.mTraceSlot.get() < mBins.size() && "a trace slot this chain keeps no bin for");
        SpriteBin& bin = mBins[inputs.mSubject.mTraceSlot.get()];
        const bool drawsSprites = inputs.mSubject.mDrawsSprites;
        SpriteSource sprites = inputs.mSubject.mScene->getBuffers().describeSprites(inputs.mSubject.mScene->getSlot());
        if (!drawsSprites)
        {
            sprites.mSpriteCount = 0;
            sprites.mEmitterCount = 0;
        }
        bin.take(sprites, what.mAsked.mCamera, commands);

        // After the take, which may have grown the tables, and once: the frame block and the
        // display's `puffsCoverNothing` read the one set.
        const SpriteTables tables = bin.getTables();

        const bool denoised = what.mReconstruction.mDenoised;
        const bool composed = what.mReconstruction.composedByTrace();

        mPasses.mVisibility.writeFrame(commands, inputs, tables, what.mSampled, composed);

        if (drawsSprites)
        {
            mPasses.mVisibility.recordSpriteShelter(commands, inputs, what.mSampled, sprites.mSpriteCount, what.mTimer);
            mPasses.mVisibility.recordSpriteEmitters(commands, inputs, sprites.mEmitterCount, what.mTimer);
        }
        bin.record(commands,
            Binning{
                .mSource = sprites,
                .mSeen = what.mAsked,
                .mTimer = what.mTimer,
            });

        mChannels->begin(commands);
        mPasses.mVisibility.record(commands, inputs, what.mSampled, what.mTimer);
        mPasses.mVisibility.recordBounceReuse(commands, inputs, what.mSampled, what.mTimer);
        mChannels->handOver(commands);

        // Where the bounce, the lobe's light and the layers' ended up: the filters' answers, or the
        // channels the trace wrote where nothing filtered them. **An unfiltered frame still turns
        // the histories**, with nothing running, so every filter is fresh at its next run: without
        // the turn, that run would read the history of the frame before this one as last frame's.
        if (!denoised)
            mDenoise.turn(TemporalFlags{});
        const Denoised resolved = denoised
            ? mPasses.mDenoise.record(commands, mDenoise, *mChannels, what.mSampled, inputs.mSubject.mMapped,
                inputs.mSubject.mLamps, what.mReconstruction, what.mTimer)
            : Denoised::unfiltered(*mChannels);

        // **Only where something is left to do**: a filter to put the albedo back in behind, a reused
        // bounce to put back, or a sum to add the frame to. Anything else was composed by the trace,
        // into the channel that is the frame, and every pass after it reads the channel as
        // `handOver` left it.
        const Image& frame = mChannels->get(Channel::Direct);
        ImageUse leftAs = Use::sAnyShaderRead;
        if (!composed || what.mAccumulate > 0)
        {
            // Written over, where the hand-over left it to be read: nothing has read it since, and
            // this is the dependency that keeps it so.
            frame.transition(commands, Use::sAnyShaderRead, Use::sComputeReadWrite);

            openZone(what.mTimer, commands, "composite");
            mPasses.mComposite.record(commands, *mChannels, resolved, mSum.isEmpty() ? nullptr : &mSum,
                Shaders::CompositeConstants{
                    .mWidth = what.mSampled.mEyes.mWorld.mWidth,
                    .mHeight = what.mSampled.mEyes.mWorld.mHeight,
                    .mAccumulate = what.mAccumulate,
                    .mComposed = composed ? 1u : 0u,
                });
            closeZone(what.mTimer, commands);

            // Whatever comes next reads what the composite just wrote. The frame's scope is the
            // wider of the two — an upscaler, a lens and a curve against a picture's one curve —
            // and covers both.
            frame.transition(commands, Use::sComputeReadWrite, Use::sAnyGeneralRead);
            leftAs = Use::sAnyGeneralRead;
        }

        return TraceResult{
            .mInputs = inputs, .mColour = HandedImage{ .mImage = frame, .mLeftAs = leftAs }, .mSprites = tables
        };
    }
}
