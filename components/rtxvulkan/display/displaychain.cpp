#include "displaychain.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <variant>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/frame/camera.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/shaders/shared/tone.h>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/tracerecording.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>

namespace Rtx
{
    namespace
    {
        /// The display pass's own description of the frame, on the picture's grid. The alpha
        /// carries the puffs' transmittance, and where the puffs' composite drew nothing the pass
        /// finds out for itself from what it is handed here.
        ///
        /// **The traced extent is the trace's camera's, and not the channels' own size.** A picture
        /// is traced into a chain grown to the largest one asked for, so a smaller picture fills a
        /// corner of its channels: read at the channels' size, the pass looked for a pixel's backdrop
        /// and its sprite tile under another pixel altogether.
        ///
        /// @param look the frame's, or null for a picture inside the interface, which takes no glare,
        ///        no gamma, no Night-Eye and no dither: the interface draws it as it draws its own.
        Shaders::ToneConstants toneFor(const Shaders::VisibilityConstants& frame, const FrameLook* look,
            const bool upscaled, const VkDeviceAddress spriteTileList, const VkDeviceAddress spritePresence,
            const VkDeviceAddress textureTexels, const VkDeviceAddress blueNoise, std::uint32_t width,
            std::uint32_t height)
        {
            const SunGlare fader = look != nullptr ? look->mGlare.mFader : SunGlare{};

            assert(spriteTileList != 0 && spritePresence != 0 && "a curve told no tiles to test the puffs by");
            assert(textureTexels != 0 && "a curve told no texel counts to test the star sheet by");

            return Shaders::ToneConstants{
                .mSpriteTileList = spriteTileList,
                .mSpritePresence = spritePresence,
                .mTextureTexels = textureTexels,
                .mBlueNoise = blueNoise,
                .mTracedWidth = frame.mEyes.mWorld.mWidth,
                .mTracedHeight = frame.mEyes.mWorld.mHeight,
                .mBackdrop = frame.mTransparentBackground == 0 ? Shaders::BACKDROP_STARS : Shaders::BACKDROP_INTERFACE,
                .mCamera = Shaders::cameraOnGrid(frame.mEyes.mWorld, width, height),
                .mStars = frame.mStars,
                .mGlareColour = fader.mFader.mColour,
                .mGlareAmount = fader.amountFor(frame),
                .mInverseGamma = look != nullptr ? look->mInverseGamma : 1.0f,
                .mNightEye = look != nullptr ? look->mNightEye : osg::Vec3f(),
                .mLiftOffset = upscaled ? -frame.mEyes.mWorld.mJitter : osg::Vec2f(),
                .mFrame = frame.mFrame,
                .mDitherStep = look != nullptr && look->mDither ? Shaders::TONE_DITHER_STEP : 0.0f,
            };
        }
    }

    DisplayChain::DisplayChain(
        const Device& device, const VisibilityPass& puffs, const VkDescriptorSetLayout textureLayout)
        : mPuffs(puffs)
        , mBloom(device)
        , mExposure(device)
        , mSunGlare(device)
        , mTone(device, textureLayout)
        , mLines(device)
    {
    }

    void DisplayChain::resize(const std::uint32_t width, const std::uint32_t height)
    {
        mBloom.resize(width, height);
    }

    void DisplayChain::beginGlare(const VkCommandBuffer commands) const
    {
        mSunGlare.begin(commands);
    }

    void DisplayChain::record(const VkCommandBuffer commands, const Display& what)
    {
        const VisibilityInputs& inputs = what.mTrace.mInputs;
        const Image& shown = what.mShown.mImage;
        assert(shown.getWidth() >= what.mExtent.width && shown.getHeight() >= what.mExtent.height);

        const FrameLook* const look = what.mFrame.has_value() ? &*what.mFrame : nullptr;
        GpuTimer* const timer = look != nullptr ? &look->mTimer : nullptr;

        // Written whole before anything reads it. The last frame may still be reading it, and the
        // head barrier `CommandPool::begin` recorded is what orders this image after it, so the
        // discard itself waits for nothing.
        what.mTarget.transition(commands, Use::sUndefined, Use::sComputeWrite);

        // The puffs over the picture, at its own extent, and then the picture is what the lens
        // spreads and the curve maps. The bloom samples what this leaves, rather than loading it —
        // `BloomPass` binds the frame as a combined image sampler — so the scope after it names
        // both reads.
        const GBuffer& channels = inputs.mChannels;

        shown.transition(commands, what.mShown.mLeftAs, Use::sTraceReadWrite);
        mPuffs.recordSpriteComposite(commands, inputs, shown, what.mSampled.mEyes, what.mExtent, timer);
        shown.transition(commands, Use::sTraceReadWrite, Use::sComputeReadOrSample);

        // What the lens will spread, built here and applied by the curve. Nothing is written back
        // over the frame — `BloomPass` says why the trace's own answer has to reach `readComposite`
        // untouched.
        if (look != nullptr)
        {
            openZone(timer, commands, FrameZone::Bloom);
            mBloom.record(commands, shown, mExposure.getExposure());
            closeZone(timer, commands);
        }

        // Measured off the image the curve is about to map, which is the upscaled one wherever
        // something upscales — see `histogram.comp` for what measuring the other one costs. One
        // `mShown` feeds both, so the two cannot come apart. A picture is measured off nothing, or
        // the same armour would be a different brightness in two windows.
        const Buffer* exposure = &mExposure.getPictureExposure();
        if (look != nullptr)
        {
            openZone(timer, commands, FrameZone::Exposure);
            if (const auto* fixed = std::get_if<FrameLook::Fixed>(&look->mExposure); fixed != nullptr)
                mExposure.recordFixed(commands, fixed->mValue);
            else if (std::holds_alternative<FrameLook::Held>(look->mExposure))
            {
                // Nothing recorded: the buffer holds what the last write left, and the head barrier
                // `CommandPool::begin` records orders this frame's curve after it.
            }
            else
            {
                const FrameLook::Measured& measured = std::get<FrameLook::Measured>(look->mExposure);
                mExposure.record(commands, shown, measured.mSeconds,
                    mExposureStale ? std::optional(measured.mStart) : std::nullopt, measured.mBias);
                mExposureStale = false;
            }
            closeZone(timer, commands);
            exposure = &mExposure.getExposure();
        }

        // What the eye saw of the sun's quad, eased at the query's own rate, which the curve lays
        // the glare fader over the picture by. Read after the trace and before the curve, on the
        // device: a frame's own count is a frame's own wash.
        const Buffer* share = &mSunGlare.getNoShare();
        if (look != nullptr)
        {
            openZone(timer, commands, FrameZone::Glare);
            mSunGlare.record(commands, look->mGlare.mSeconds, mGlareStale);
            mGlareStale = false;
            closeZone(timer, commands);
            share = &mSunGlare.getShare();
        }

        const Shaders::ToneConstants constants = toneFor(what.mSampled, look, what.mUpscaled,
            what.mTrace.mSprites.mTileList, what.mTrace.mSprites.mPresence, inputs.mSubject.mScene->getTextureTexels(),
            mPuffs.getBlueNoise(), what.mExtent.width, what.mExtent.height);
        const auto toneInto = [&](const Image& target, const Shaders::ToneConstants& into) {
            mTone.record(commands,
                Tone{
                    .mColour = shown,
                    .mExposure = *exposure,
                    .mSunGlare = *share,
                    .mBackdrop = channels.get(Channel::Backdrop),
                    .mSurface = channels.get(Channel::Surface),
                    .mLift = channels.get(Channel::Lift),
                    .mBloom = look != nullptr ? mBloom.getPyramid() : nullptr,
                    .mTextures = inputs.mSubject.mScene->getTextures(),
                    .mTarget = target,
                    .mConstants = into,
                });
        };

        openZone(timer, commands, FrameZone::Tone);
        toneInto(what.mTarget, constants);

        // **The curve run a second time, because a copy of the picture would carry its byte**, and
        // the byte is what the second store is there to escape: a summed frame's mean falls between
        // the byte's levels. Undithered, since sixteen bits round under anything measured against
        // them.
        if (look != nullptr && look->mDeep != nullptr)
        {
            Shaders::ToneConstants deep = constants;
            deep.mDitherStep = 0.0f;
            look->mDeep->transition(commands, Use::sUndefined, Use::sComputeWrite);
            toneInto(*look->mDeep, deep);
            look->mDeep->transition(commands, Use::sComputeWrite, what.mLeftAs);
        }
        closeZone(timer, commands);

        if (look != nullptr)
            recordDebugLines(commands, what, *look);

        what.mTarget.transition(commands, Use::sComputeWrite, what.mLeftAs);
    }

    void DisplayChain::recordDebugLines(const VkCommandBuffer commands, const Display& what, const FrameLook& look)
    {
        const DebugLines& debug = look.mDebug;
        if (debug.empty())
            return;

        const GBuffer& channels = what.mTrace.mInputs.mChannels;

        // The lines first and the triangles after them, in the slot's own buffer: the frame
        // behind read its own slot's, so nothing here is written under a submit.
        const std::size_t count = debug.mLines.size() + debug.mTriangles.size();
        look.mDebugVertices.outgrow(count * sizeof(DebugVertex));

        const std::span<DebugVertex> written = look.mDebugVertices.get().writable<DebugVertex>(0, count);
        std::copy(debug.mLines.begin(), debug.mLines.end(), written.begin());
        std::copy(debug.mTriangles.begin(), debug.mTriangles.end(), written.begin() + debug.mLines.size());

        openZone(&look.mTimer, commands, FrameZone::Lines);

        // Drawn over what the curve wrote, and left where the curve left it, for the chain's last
        // transition to take it from.
        Image& target = what.mTarget;
        target.transition(commands, Use::sComputeWrite, Use::sColourAttachment);

        // `screenOf` divides by the distance ahead, which is the perspective divide.
        assert(what.mSampled.mEyes.mWorld.mOrthographic == 0 && "debug lines through a parallel projection");
        mLines.record(commands,
            Lines{
                .mTarget = target,
                .mSurface = channels.get(Channel::Surface),
                .mConstants = {
                    .mScreen = screenBasisOf(what.mSampled.mEyes.mWorld.mBasis),
                    .mExtent = Shaders::uvec2(what.mExtent.width, what.mExtent.height),
                    .mOrigin = what.mSampled.mOrigin,
                    .mNear = what.mSampled.mNear,
                    .mTraced = Shaders::uvec2(what.mSampled.mEyes.mWorld.mWidth, what.mSampled.mEyes.mWorld.mHeight),
                    .mInverseGamma = look.mInverseGamma,
                },
                .mVertices = look.mDebugVertices.get(),
                .mLineCount = static_cast<std::uint32_t>(debug.mLines.size()),
                .mTriangleCount = static_cast<std::uint32_t>(debug.mTriangles.size()),
            });

        target.transition(commands, Use::sColourAttachment, Use::sComputeWrite);

        closeZone(&look.mTimer, commands);
    }
}
