#pragma once

#include <cstdint>
#include <optional>

#include <osg/Vec2f>

#include "debuglines.hpp"
#include "framepast.hpp"
#include "reconstruction.hpp"
#include "surfaceview.hpp"

namespace Rtx
{
    /// What a frame is asked for, beyond where the camera stands.
    ///
    /// **Nothing the renderer's profile states is repeated here.** A field that stands over the
    /// profile is empty unless this frame asks otherwise, which is how a reference and the frame it
    /// is compared against come off one renderer; the renderer reads its own profile for the rest.
    /// A host that read the profile back to hand it in again was a host that could forget to, and
    /// a frame that forgot traced what nobody asked.
    struct FrameOptions
    {
        /// How many frames have gone into the running sum, this one included; zero is no averaging.
        /// The sum is kept in floating point, because eight bits would round every sample and clip
        /// the sun's disc.
        std::uint32_t mAccumulate = 0;

        /// How long since the frame before this one was traced, in seconds of the host's clock
        /// (`Misc::LoopClock::getNow`): what the eye adapts over, the glare fades over and the upscaler tunes
        /// itself by. Nought is a frame with nothing before it, which adapts nothing.
        ///
        /// **The host's and never the wall read here**, so a measured run, whose clock counts frames, draws
        /// the same pictures however fast it drew them; one rule in both modes, so frames nobody traced are
        /// time the eye had.
        float mSinceLast = 0.0f;

        /// What this frame's past is worth, as the host tells it: the one route by which a loss
        /// reaches the histories, each of which reads its column of `FramePast`.
        HistoryLoss mLoss = HistoryLoss::None;

        /// What the frame asks of the reconstruction in place of the profile's, before the
        /// upscaler has its say — `Reconstruction::resolve` is the rule.
        std::optional<ReconstructionRequest> mReconstruction{};

        /// How the frame is scaled before the display curve, in place of the profile's rule.
        std::optional<ExposureRule> mExposure{};

        /// What every pixel is painted with, and whether an environment map's sheet is lit, in place of
        /// the profile's: what a test of one surface input asks for.
        std::optional<SurfaceView> mShow{};
        std::optional<bool> mLitEnvironmentMaps{};

        /// Whether the display curve dithers its store, in place of the profile's: what a test of
        /// the dither asks for, over a harness that turns it off.
        std::optional<bool> mDither{};

        /// Where in the pixel the frame samples, where the reconstruction does not jitter: a test's
        /// fixed sub-pixel offset. Nothing samples the pixel's centre. A reconstruction that jitters
        /// walks its own sequence, and a frame that asked for both is an assert.
        std::optional<osg::Vec2f> mJitter{};

        /// What the game's debug modes drew, over the picture and under the interface. A tool and
        /// not the picture: nothing traces it, and a frame with none pays nothing for it.
        DebugLines mDebug{};

        /// Whether the frame leaves its picture in host memory for `FrameResult::mPixels`, copied by the
        /// frame's own commands after the display curve and before the interface, and digests what it traced
        /// for `FrameResult::mDigest`. For a run that hashes every frame: the copy rides the queue behind the
        /// trace, where a readback of the frame just drawn is a submit and a wait of its own.
        bool mReadBack = false;
    };
}
