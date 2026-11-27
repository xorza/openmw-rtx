#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <osg/Vec2f>
#include <osg/Vec2i>
#include <vulkan/vulkan_core.h>

#include <components/rtx/scene/ripple.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/ripple.h>
#include <components/rtxvulkan/texture/mipchainpass.hpp>

namespace Rtx
{
    class Device;
    class GpuTimer;
    class Uploads;

    /// What walked through the water, as a height field around the eye the sea reads beside its cascades: stepped by
    /// the wave equation, pressed where the game says something disturbed it, and unpacked into a slope tile and a
    /// curvature tile with a chain each, in the wave tiles' own layout. `shaders/ripple.h` says whose field this is.
    /// State across frames, and deterministic: the steps are by the water's clock and the impulses by the simulation,
    /// so two runs of one build stand the same field on every frame.
    ///
    /// The clock is `Shaders::VisibilityConstants::mWaterTime`, the simulation's seconds, which upstream's
    /// `RipplesSurface::updateState` steps by. Not the sky's, which stands still in every interior with water in it and
    /// races under a sped-up `timescale`.
    class RipplePass
    {
    public:
        /// Makes the field still, and leaves every tile in the layout the trace samples it in.
        /// Submits and waits.
        explicit RipplePass(const Device& device);

        /// Steps the field once for every whole sixtieth `waterSeconds` passed since the last frame, no more than
        /// `RIPPLE_FRAME_STEPS_MOST`, presses with the first every impulse kept since the last step, and unpacks the
        /// tiles; a frame that passes no sixtieth keeps `impulses` for the next that does. The window follows `eye` by
        /// whole texels. Nothing at all where the clock did not move: a press stands for its frame's time
        /// (`GpuRippleImpulse::mPress`), and that frame's water had none.
        ///
        /// The impulses that reach the window are pressed in the order the game gave them, and past
        /// `RIPPLE_IMPULSES_MOST` the nearest the eye are kept: one whose ring presses no texel of it is no impulse.
        /// Nothing is done while the field is still and nothing presses it: once a frame's last step reports every
        /// texel nought (`RIPPLE_STILL`) each step would step nought to nought, so the frame skips both, exactly.
        ///
        /// @param uploads what writes the impulses, ordered ahead of the steps that read them.
        /// @param slot the frame's, which names the copy of the impulse buffer this frame writes.
        /// @param timer where the step's zone goes, or nothing where nobody is counting. Opened
        ///        only on a frame that steps, so a frame that stands still reports no zone.
        void record(VkCommandBuffer commands, Uploads& uploads, FrameSlot slot, std::span<const RippleImpulse> impulses,
            const osg::Vec2f& eye, double waterSeconds, GpuTimer* timer);

        /// Drops what the field holds, for a world that was replaced rather than moved through.
        void reset() { mReset = true; }

        /// Whether the field is known to be nought everywhere, so the frames skip it until
        /// something presses it again.
        bool isStill() const { return mStill; }

        /// Linear, mipmapped and clamped to a border of nothing: past the field's edge is still
        /// water.
        VkSampler getSampler() const { return mSampler.get(); }

        const Image& getSurface() const { return mSurface; }
        const Image& getCurvature() const { return mCurvature; }

        /// Where the field's window begins, in world units, and how wide it is.
        const osg::Vec2f& getOrigin() const { return mOrigin; }
        static float getExtent() { return Shaders::RIPPLE_TEXEL * static_cast<float>(Shaders::RIPPLE_GRID); }

    private:
        /// Which texel the window would begin at for an eye at `eye`: the eye's own texel less
        /// half the grid, so the eye stands in the middle.
        static osg::Vec2i windowOf(const osg::Vec2f& eye);

        /// Where `window` begins, in world units.
        static osg::Vec2f originOf(const osg::Vec2i& window);

        /// Moves the window to `window`, in texels and in world units together.
        void standWindow(const osg::Vec2i& window);

        const Device& mDevice;

        ComputePipeline<Shaders::RippleStepConstants> mStepPipeline;
        ComputePipeline<NoConstants> mComposePipeline;
        MipChainPass mMips;

        Sampler mSampler;

        /// The two heights, this step's and the one before, ping-ponged between two images so a
        /// step reads one whole and writes the other.
        std::array<Image, 2> mFields;
        std::size_t mLatest = 0;

        Image mSurface;
        Image mCurvature;

        /// The step's impulses, one copy a frame slot so a write never lands under a submit, and
        /// those kept since the last step, each with how long its frame stood, the eye's distance
        /// when it was given and the game's order, for choosing the nearest.
        PerSlot<Buffer> mImpulses;
        std::vector<Shaders::GpuRippleImpulse> mImpulseScratch;
        struct Candidate
        {
            float mDistance{};
            std::uint32_t mOrder{};
            RippleImpulse mImpulse;
            float mPress{};
        };
        std::vector<Candidate> mCandidates;

        /// The order the next impulse given is kept in, from nought after each step.
        std::uint32_t mOrder = 0;

        /// Whether the field moves once a frame's last step has written it, a word a frame slot,
        /// and which frame wrote each, nothing for a slot whose frame took no step.
        PerSlot<Buffer> mMoving;
        PerSlot<std::optional<std::uint64_t>> mReportedAt;

        /// The frames this has recorded, the last of them that pressed an impulse, and whether the
        /// field is known to be nought everywhere.
        std::uint64_t mFrames = 0;
        std::uint64_t mPressedAt = 0;
        bool mStill = false;

        /// Where the window begins, in texels and in world units.
        osg::Vec2i mWindow;
        osg::Vec2f mOrigin;

        /// The water's clock at the last frame it moved on, whose whole sixtieths are stepped or dropped.
        double mSeenSeconds = 0.0;
        bool mReset = true;
    };
}
