#pragma once

#include <cstdint>
#include <optional>

#include <osg/Vec2f>

#include "debuglines.hpp"
#include "reconstruction.hpp"
#include "sunglare.hpp"
#include "surfaceview.hpp"

namespace Rtx
{
    /// What a frame is asked for, beyond where the camera stands.
    ///
    /// **Nothing the renderer's profile states is repeated here.** A field that stands over the
    /// profile is empty unless this frame asks otherwise, which is how a reference and the frame it
    /// is compared against come off one renderer; the renderer reads its own profile for the rest.
    /// A host that read the profile back to hand it in again was a host that could forget to, and
    /// a frame that forgot traced with a delight of nought.
    struct FrameOptions
    {
        /// How many frames have gone into the running sum, this one included; zero is no averaging.
        /// The sum is kept in floating point, because eight bits would round every sample and clip
        /// the sun's disc.
        std::uint32_t mAccumulate = 0;

        /// How long since the frame before this one was traced, in seconds of the host's clock
        /// (`Misc::FrameClock::getNow`): what the eye adapts over, the glare fades over and the
        /// upscaler tunes itself by. Nought is a frame with nothing before it, which adapts nothing.
        ///
        /// **The host's and never the wall read here**, so a measured run, whose clock counts
        /// frames, draws the same pictures however fast it drew them; and one rule in both modes,
        /// so frames nobody traced — a menu over a hidden world — are time the eye had.
        float mSinceLast = 0.0f;

        /// What to multiply the measured exposure by: the hour, which the histogram cannot see
        /// (`Rtx::Skylight::mExposureBias`). A fixed exposure is not touched by it.
        float mExposureBias = 1.0f;

        /// The sun glare fader over the picture, which the display chain washes it with. None for a
        /// frame the world did not describe.
        SunGlare mGlare;

        /// What the frame asks of the reconstruction in place of the profile's, before the
        /// upscaler has its say — `Reconstruction::resolve` is the rule.
        std::optional<ReconstructionRequest> mReconstruction;

        /// How the frame is scaled before the display curve, in place of the profile's rule.
        std::optional<ExposureRule> mExposure;

        /// How much painted lighting to divide out, and what every pixel is painted with, in place of
        /// the profile's: what a test of one surface input asks for.
        std::optional<float> mDelight;
        std::optional<SurfaceView> mShow;

        /// Where in the pixel the frame samples, where the reconstruction does not jitter: a test's
        /// fixed sub-pixel offset. Nothing samples the pixel's centre. A reconstruction that jitters
        /// walks its own sequence, and a frame that asked for both is an assert.
        std::optional<osg::Vec2f> mJitter;

        /// What the game's debug modes drew, over the picture and under the interface. A tool and
        /// not the picture: nothing traces it, and a frame with none pays nothing for it.
        DebugLines mDebug;

        /// Whether the frame leaves its picture in host memory for `FrameResult::mPixels`,
        /// copied by the frame's own commands after the display curve and before the interface,
        /// and digests what it traced for `FrameResult::mDigest` on the way. For a run that hashes
        /// every frame: a copy the frame records rides the queue behind the trace and comes back
        /// with the frame's report, where a readback of the frame just drawn is a submit of its
        /// own and a wait the ring would otherwise overlap.
        bool mReadBack = false;
    };
}
